"""Protect existing client INI bytes while changing the optional scaling mode."""
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('deploy_upscaling', root/'tools/deploy-upscaling.py')
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)

for newline in (b'\n', b'\r\n'):
    prefix = newline.join((b'; ANSI comment: \xb1\xb1\xb6\xb7', b'[general]',
                           b'width=3612', b'height=2032', b'render_width=1920',
                           b'render_height=1080', b'', b''))
    suffix = newline.join((b'[auto_login]', b'username=fixture-only', b'', b''))
    middle = newline.join((b'[upscaling]', b'algorithm=cunny', b'quality=balanced',
                           b'enabled=true', b'max_fps=60', b'diagnostics=false', b'; keep me', b'', b''))
    before = prefix+middle+suffix
    after = deploy.configure(before, 'balanced', 'linear', True)
    assert after.startswith(prefix) and after.endswith(suffix)
    assert b'algorithm=linear'+newline in after and b'diagnostics=true'+newline in after
    assert b'max_fps=60'+newline in after and b'; keep me'+newline in after
    assert b'diagnostics=false' not in after
    assert deploy.configure(after, 'balanced', 'linear', True) == after
    restored = deploy.configure(after, 'balanced', 'cunny', False)
    assert b'algorithm=cunny'+newline in restored and b'diagnostics=false'+newline in restored
    assert restored.startswith(prefix) and restored.endswith(suffix)
    inherited = deploy.configure(before, 'fast', 'linear')
    assert b'diagnostics=false'+newline in inherited
    appended = deploy.configure(prefix+suffix, 'balanced', 'linear')
    assert appended.startswith(prefix+suffix)

for raw in (b'\xff\xfe[\0x\0]', b'[upscaling]\n[UPSCALING]\n'):
    try:
        deploy.configure(raw, 'balanced', 'linear')
    except ValueError:
        pass
    else:
        raise AssertionError('Ambiguous or unsupported INI accepted')
print('PASS deployment config: ANSI bytes, unrelated sections, LF/CRLF, diagnostics, repeated mode switch, malformed input')
