import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from runtime_fixture import generate

root = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix='runtime-test-', dir=root / '.local'))
generate(work / 'valid')
host = root / '.local/runtime/hayarigami-host'


def run(case, expected, message):
    p = subprocess.run([str(host), str(work / case), '--fixture'], capture_output=True, text=True, timeout=10)
    if p.returncode != expected:
        raise AssertionError((case, p.returncode, p.stdout, p.stderr))
    if message not in p.stdout + p.stderr:
        raise AssertionError((case, p.stdout, p.stderr))


run('valid', 0, 'returned 42')
shutil.copytree(work / 'valid', work / 'code-relocation')
path = work / 'code-relocation/fixture.patched.bin'
data = bytearray(path.read_bytes())
struct.pack_into('<Q', data, 0x4600, 0x100)
path.write_bytes(data)
run('code-relocation', 42, 'relocation into code or keys')
shutil.copytree(work / 'valid', work / 'bad-mod0')
path = work / 'bad-mod0/fixture.patched.bin'
data = bytearray(path.read_bytes())
struct.pack_into('<I', data, 4, 0xffffffff)
path.write_bytes(data)
run('bad-mod0', 42, 'image range out of bounds')
print('PASS: native execution with relocation; invalid code relocation and MOD0 rejected')
