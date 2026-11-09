"""Create the small app-only ZIP understood by the on-console updater."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

p = argparse.ArgumentParser()
p.add_argument('executable', type=Path)
p.add_argument('output', type=Path)
args = p.parse_args()
data = args.executable.read_bytes()
if data[:4] != b'XEX2' or not 1024 <= len(data) <= 16*1024*1024-116:
    raise SystemExit('Invalid executable or updater size limit exceeded')
args.output.parent.mkdir(parents=True, exist_ok=True)
info = zipfile.ZipInfo('360AI.xex', date_time=(1980, 1, 1, 0, 0, 0))
info.compress_type = zipfile.ZIP_STORED
info.create_system = 0
info.external_attr = 0x20
with zipfile.ZipFile(args.output, 'w', allowZip64=False) as archive:
    archive.writestr(info, data)
with zipfile.ZipFile(args.output) as archive:
    assert archive.namelist() == ['360AI.xex']
    assert archive.testzip() is None
    assert archive.read('360AI.xex') == data
result = args.output.read_bytes()
assert len(result) == len(data)+116
print(json.dumps({'name': args.output.name, 'size': len(result), 'sha256': hashlib.sha256(result).hexdigest()}))
