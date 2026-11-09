"""Independent ZIP fixtures for updater parser, tamper and recovery checks."""
from pathlib import Path
from io import BytesIO
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parent.parent / 'build/tests/updates'
root.mkdir(parents=True, exist_ok=True)
old = b'XEX2' + b'old app'.ljust(2044, b'\0')
new = b'XEX2' + b'new app'.ljust(4092, b'\0')
def zip_bytes(name='360AI.xex', payload=new, method=zipfile.ZIP_STORED, extra=False):
    stream = BytesIO()
    with zipfile.ZipFile(stream, 'w') as z:
        z.writestr(name, payload, compress_type=method)
        if extra:
            z.writestr('360AI.json', b'{}')
    return stream.getvalue()
valid = zip_bytes()
cases = {'normal': valid, 'resume': valid, 'rollback': valid, 'tamper': valid,
         'stale': valid, 'idempotent': valid, 'damaged': valid,
         'traversal': zip_bytes('../360AI.xex'), 'compressed': zip_bytes(method=zipfile.ZIP_DEFLATED),
         'extra': zip_bytes(extra=True), 'magic': zip_bytes(payload=b'NOPE'+new[4:]),
         'truncated': valid[:-12], 'trailing': valid+b'trailing garbage',
         'crc': valid[:50]+bytes([valid[50]^1])+valid[51:]}
for name, content in cases.items():
    folder = root / name
    folder.mkdir(exist_ok=True)
    for file in folder.iterdir():
        if file.is_file():
            file.unlink()
    (folder / '360AI.xex').write_bytes(old)
    (folder / '360AI-update.zip').write_bytes(content)
    (folder / '360AI.json').write_text('personal settings', encoding='ascii')
    (folder / '360AI-assistant.json').write_text('personal notes', encoding='ascii')
    (folder / 'model.gguf').write_bytes(b'personal model')
    (folder / 'fixture.json').write_text(json.dumps({'size':len(content), 'sha256':hashlib.sha256(content).hexdigest()}))
(root / 'expected.json').write_text(json.dumps({'old':hashlib.sha256(old).hexdigest(), 'new':hashlib.sha256(new).hexdigest()}))
print('Prepared', len(cases), 'update fixtures')
