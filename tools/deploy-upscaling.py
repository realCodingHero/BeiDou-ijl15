"""Install the optional local DLL, backing up files and preserving INI encoding."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil


def configure(raw, quality):
    if raw.startswith((b'\xff\xfe', b'\xfe\xff')) or b'\x00' in raw:
        raise ValueError('This client requires an ANSI/UTF-8 INI, not UTF-16')
    newline = b'\r\n' if b'\r\n' in raw else b'\n'
    lines = raw.splitlines(keepends=True)
    section = re.compile(rb'^\s*\[([^\]]+)\]')
    starts = [i for i, line in enumerate(lines) if (m := section.match(line)) and m[1].strip().lower() == b'upscaling']
    if len(starts) > 1:
        raise ValueError('Multiple [upscaling] sections; resolve before deployment')
    settings = {b'enabled': b'true', b'algorithm': b'cunny', b'quality': quality.encode('ascii')}
    entries = [key+b'='+value+newline for key, value in settings.items()]
    if not starts:
        return raw + (b'' if not raw or raw.endswith(b'\n') else newline) + newline + b'[upscaling]'+newline+b''.join(entries)
    start = starts[0]
    end = next((i for i in range(start+1, len(lines)) if section.match(lines[i])), len(lines))
    retained = [line for line in lines[start+1:end] if not re.match(rb'^\s*(enabled|algorithm|quality)\s*=', line, re.I)]
    header = lines[start] if lines[start].endswith(b'\n') else lines[start]+newline
    return b''.join(lines[:start]+[header]+entries+retained+lines[end:])


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--quality', choices=['fast', 'balanced'], default='balanced')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    client = args.client.resolve(strict=True)
    config = client/'config.ini'
    if not (client/'BeiDou.exe').is_file() or not config.is_file():
        raise SystemExit('Expected an existing BeiDou client directory')
    if (client/'d3d8.dll').exists():
        raise SystemExit('An application-local d3d8.dll conflicts with this client; remove that wrapper first')
    binaries = {'BeiDouUpscale.dll': root/'out/neural/BeiDouUpscale.dll',
                'ijl15.dll': root/'out/window-scaling/ijl15.dll'}
    for name, source in binaries.items():
        binary = source.read_bytes()
        pe = int.from_bytes(binary[60:64], 'little')
        if binary[:2] != b'MZ' or binary[pe:pe+4] != b'PE\0\0' or binary[pe+4:pe+6] != b'\x4c\x01':
            raise SystemExit(f'Expected a built x86 PE DLL: {name}')
    original = config.read_bytes()
    updated = configure(original, args.quality)
    backup = client/'upscaling-backups'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup.mkdir(parents=True)
    shutil.copy2(config, backup/'config.ini')
    for name in binaries:
        if (client/name).exists():
            shutil.copy2(client/name, backup/name)
    manifest = {'client': str(client), 'installed_sha256': {name: sha(source) for name,source in binaries.items()},
                'config_before_sha256': sha(config), 'source_directory': str(root), 'quality': args.quality}
    notices = client/'upscaling-licenses'
    notices.mkdir(exist_ok=True)
    for source, target in [('LICENSE','BeiDou-AGPL-3.0.txt'), ('third_party/d3d8to9/LICENSE.md','d3d8to9-BSD-2-Clause.txt'),
                           ('third_party/cunny/LICENSE','CuNNy-LGPL-3.0.txt'), ('third_party/cunny/COPYING','CuNNy-GPL-3.0.txt'),
                           ('third_party/README.md','THIRD-PARTY.md')]:
        shutil.copy2(root/source, notices/target)
    shutil.copy2(root/'docs/built-in-upscaling.md', notices/'USAGE.md')
    # Do not overwrite an INI changed by a running client during the backup step.
    if config.read_bytes() != original:
        raise SystemExit('Config changed during deployment; backup saved, retry after the game exits')
    replaced = []
    try:
        for name, source in binaries.items():
            shutil.copy2(source, client/name)
            replaced.append(name)
        config.write_bytes(updated)
    except OSError:
        # A loaded DLL is normally locked. Restore any completed earlier copy.
        for name in reversed(replaced):
            if (backup/name).exists():
                shutil.copy2(backup/name, client/name)
            else:
                (client/name).unlink()
        if config.read_bytes() != original:
            config.write_bytes(original)
        raise
    manifest['config_after_sha256'] = sha(config)
    (backup/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    for name in binaries:
        print('Installed:', client/name, 'SHA256:', sha(client/name))
    print('Backup:', backup)
    print('Source:', root)
    print('Restart the client to load. To disable: [upscaling] enabled=false, then restart.')


if __name__ == '__main__':
    main()
