"""Audit production resolution writes against x86 operands in a supported EXE.

Requires Capstone 5. --write regenerates the reviewed manifest; without it the
audit fails if source sites, instructions, or the checked-in manifest drift.
The executable is read only and is never loaded or run by this script.
"""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]


class PE:
    def __init__(self, path):
        self.data = path.read_bytes()
        pe = self.u32(60)
        assert self.data[:2] == b'MZ' and self.data[pe:pe+4] == b'PE\0\0'
        self.machine, count = struct.unpack_from('<HH', self.data, pe+4)
        self.timestamp = self.u32(pe+8)
        opt = pe+24
        assert self.machine == 0x14c and self.data[opt:opt+2] == b'\x0b\x01'
        self.base, self.size = self.u32(opt+28), self.u32(opt+56)
        sh = opt+struct.unpack_from('<H', self.data, pe+20)[0]
        self.sections = []
        for i in range(count):
            o = sh+40*i
            name, vs, va, rs, raw = struct.unpack_from('<8sIIII', self.data, o)
            self.sections.append((name.rstrip(b'\0').decode(), va, max(vs, rs), raw, rs, self.u32(o+36)))

    def u32(self, offset):
        return struct.unpack_from('<I', self.data, offset)[0]

    def section(self, address):
        return next(s for s in self.sections if s[1] <= address-self.base < s[1]+s[2])

    def read(self, address, count):
        _, va, size, raw, rs, _ = self.section(address)
        off = address-self.base-va
        assert off+count <= size
        real = max(0, min(count, rs-off))
        return self.data[raw+off:raw+off+real]+bytes(count-real)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--capstone-path')
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    if args.capstone_path:
        sys.path.insert(0, args.capstone_path)
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_GRP_CALL, CS_GRP_JUMP
    image = PE(args.exe)
    cs = Cs(CS_ARCH_X86, CS_MODE_32)
    cs.detail = True
    # Executable .text contains data too. skipdata lets the offline index resume;
    # every selected site is independently decoded again below.
    cs.skipdata = True
    instructions = {}
    for _, va, size, _, rs, flags in image.sections:
        if flags & 0x20000000:
            for ins in cs.disasm(image.read(image.base+va, rs), image.base+va):
                if ins.id:
                    instructions[ins.address] = ins
    addresses = sorted(instructions)
    syms = {}
    for path in [ROOT/'ezorsia/AddyLocations.h', ROOT/'ezorsia/codecaves.h']:
        for m in re.finditer(r'(?:DWORD|int)\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;', path.read_text(encoding='utf-8', errors='replace')):
            syms[m[1]] = int(m[2], 0)

    def value(expr):
        return sum(int(t, 0) if re.match(r'^(0x|\d)', t) else syms[t]
                   for t in re.sub(r'\s', '', expr).split('+'))

    source = (ROOT/'ezorsia/Client.cpp').read_text(encoding='utf-8')
    body = source[source.index('bool Client::UpdateResolution()'):source.index('void Client::EnableNewIGCipher()')]
    sites = []
    for line in body.splitlines():
        line = line.split('//', 1)[0]
        m = re.search(r'patches\.(WriteInt|WriteByte|WriteByteArray|CodeCave|FillBytes)\((.*)\);', line)
        if not m:
            continue
        method, argtext = m.groups()
        # Only the address/length arguments are evaluated, never arbitrary C++.
        callargs = [a.strip() for a in argtext.split(',')]
        address = value(callargs[1] if method == 'CodeCave' else callargs[0])
        kind = {'WriteInt':'Integer', 'WriteByte':'Byte', 'CodeCave':'Jump',
                'WriteByteArray':'Bytes', 'FillBytes':'Fill'}[method]
        size = 4 if kind == 'Integer' else 1
        if kind in ('Jump', 'Fill'):
            size = value(callargs[2])
        if kind == 'Bytes':
            assert address == 0x009F7A9B and callargs[1] == 'forced_window' and callargs[2] == 'sizeof(forced_window)'
            size = 5
        _, _, _, _, _, flags = image.section(address)
        guard, guard_size = address, size
        asm = 'data'
        if flags & 0x20000000:
            if kind in ('Integer', 'Byte'):
                ins = instructions[addresses[bisect.bisect_right(addresses, address)-1]]
                assert address+size <= ins.address+ins.size, f'{address:08X}: crosses instruction boundary'
                offsets = [(ins.imm_offset, ins.imm_size), (ins.disp_offset, ins.disp_size)]
                assert (address-ins.address, size) in offsets, f'{address:08X}: write is not an entire immediate/displacement: {ins.mnemonic} {ins.op_str}'
                assert not ins.group(CS_GRP_CALL) and not ins.group(CS_GRP_JUMP), f'{address:08X}: resolution cannot change branch targets'
                guard, guard_size = ins.address, ins.size
                asm = ins.mnemonic+' '+ins.op_str
            else:
                decoded = list(cs.disasm(image.read(address, size), address))
                assert all(i.id for i in decoded) and sum(i.size for i in decoded) == size, f'{address:08X}: partial instruction replacement'
                if kind == 'Jump':
                    assert size >= 5
                asm = '; '.join(i.mnemonic+' '+i.op_str for i in decoded)
        else:
            assert kind == 'Integer' and address in (0x00BE2738, 0x00BE273C, 0x00BE2DF0, 0x00BE2DF4), f'Unreviewed data write {address:08X}'
        site = dict(address=address, size=size, kind=kind, guard=guard, guard_size=guard_size,
                    original=image.read(guard, guard_size).hex(), instruction=asm)
        for previous in sites:
            if address < previous['address']+previous['size'] and previous['address'] < address+size:
                # bigLoginFrame uses an immediate; the other branch replaces its
                # entire block. Batch::Add still rejects simultaneous application.
                pair = {(address, kind), (previous['address'], previous['kind'])}
                assert pair == {(0x005F464D, 'Jump'), (0x005F464E, 'Integer')}, f'{address:08X}: unreviewed overlapping write'
        sites.append(site)
    assert len(sites) > 290, 'Resolution body extraction failed'
    sites.sort(key=lambda s: (s['address'], s['size']))
    document = dict(machine=image.machine, timestamp=image.timestamp, preferred_base=image.base,
                    image_size=image.size, exe_sha256=hashlib.sha256(image.data).hexdigest(), sites=sites)
    text = json.dumps(document, indent=2)+'\n'
    header = ['// Generated by tools/audit-resolution-patches.py from the reviewed EXE.',
              '#pragma once', '#include "ResolutionPatch.h"', 'namespace ResolutionPatch {',
              f'constexpr uint32_t kImageBase = 0x{image.base:08X};',
              f'constexpr uint32_t kImageSize = 0x{image.size:08X};',
              f'constexpr uint32_t kImageTimestamp = 0x{image.timestamp:08X};',
              'const Site kSites[] = {']
    for s in sites:
        raw = ''.join('\\x'+s['original'][i:i+2] for i in range(0, len(s['original']), 2))
        header.append(f'    {{0x{s["address"]:08X}, {s["size"]}, Kind::{s["kind"]}, 0x{s["guard"]:08X}, {s["guard_size"]}, "{raw}", "0x{s["address"]:08X}"}},')
    header += ['};', '}']
    for path, data in [(ROOT/'tests/fixtures/resolution-patches.json', text),
                       (ROOT/'ezorsia/ResolutionPatchSites.h', '\n'.join(header)+'\n')]:
        if args.write:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data, encoding='utf-8', newline='\n')
        else:
            assert path.read_text(encoding='utf-8') == data, f'Manifest drift: {path}'
    print(f'Audited {len(sites)} patch sites: operands, replacement boundaries and exclusive alternatives valid.')
    print('EXE SHA256:', document['exe_sha256'])


if __name__ == '__main__':
    main()
