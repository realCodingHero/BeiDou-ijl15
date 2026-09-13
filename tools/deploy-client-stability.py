"""Back up and install the tested instruction-integrity and ItemEff fixes.

The existing configuration and neural upscaling DLL are not rewritten.
Exit the target client before running this command.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    client = args.client.resolve(strict=True)
    config = client/'config.ini'
    profile = json.loads((root/'tests/fixtures/resolution-patches.json').read_text(encoding='utf-8'))
    if sha(client/'BeiDou.exe') != profile['exe_sha256']:
        raise SystemExit('Unsupported client EXE; no files changed')
    binaries = {'ijl15.dll':root/'out/window-scaling/ijl15.dll',
                'BeiDouItemEff.dll':root/'out/itemeff/BeiDouItemEff.dll'}
    for name, source in binaries.items():
        b = source.read_bytes(); pe = int.from_bytes(b[60:64], 'little')
        assert b[:2] == b'MZ' and b[pe:pe+6] == b'PE\0\0\x4c\x01', name
        # Loaded Windows images cannot be opened for writing. Check all targets
        # before copying any of them; this open does not truncate/write bytes.
        with (client/name).open('r+b'):
            pass
    backup = client/'stability-backups'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup.mkdir(parents=True)
    shutil.copy2(config, backup/'config.ini')
    before = {name:sha(client/name) for name in binaries}
    for name in binaries:
        shutil.copy2(client/name, backup/name)
    for log in ('patch-integrity.log','upscaling.log'):
        if (client/log).exists(): shutil.copy2(client/log, backup/log)
    manifest = dict(client=str(client), source=str(root), before=before,
                    installed={name:sha(p) for name,p in binaries.items()},
                    config_sha256=sha(config), neural_dll_sha256=sha(client/'BeiDouUpscale.dll'))
    replaced = []
    try:
        for name, source in binaries.items():
            replaced.append(name)
            shutil.copy2(source, client/name)
            assert sha(client/name) == manifest['installed'][name]
    except (OSError, AssertionError):
        for name in reversed(replaced): shutil.copy2(backup/name, client/name)
        raise
    assert sha(config) == manifest['config_sha256']
    assert sha(client/'BeiDouUpscale.dll') == manifest['neural_dll_sha256']
    (backup/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('Installed:', client)
    print('Backup:', backup)
    print('Config and neural DLL hashes unchanged.')


if __name__ == '__main__':
    main()
