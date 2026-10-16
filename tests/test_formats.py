import hashlib
import io
import struct
import sys
import unittest
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from nx_read import Section, Slice
from analyze import archive_entries, nso_info
import verify_resources


class FormatTests(unittest.TestCase):
    def test_empty_resource_scan_does_not_report_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'empty.nsp'
            source.write_bytes(b'')
            argv = ['verify_resources', '--nsp', str(source), '--keys', str(Path(tmp) / 'unused.keys'),
                    '--library', str(Path(tmp) / 'unused.dylib')]
            with patch('sys.argv', argv), patch.object(verify_resources.ctypes, 'CDLL'), \
                 patch.object(verify_resources, 'load_keys', return_value={}), \
                 patch.object(verify_resources, 'pfs_entries', return_value=[]):
                with self.assertRaisesRegex(ValueError, 'zero coverage'):
                    verify_resources.main()

    def test_ctr_random_access_nonzero_base_and_unaligned_read(self):
        key = bytes(range(16))  # synthetic test key, never a game key
        header = bytearray(512)
        header[4] = 3
        header[0x140:0x148] = b'12345678'
        absolute = 512
        plain = bytes(range(256)) * 2
        ctr = header[0x140:0x148][::-1] + (absolute // 16).to_bytes(8, 'big')
        enc = Cipher(algorithms.AES(key), modes.CTR(ctr)).encryptor()
        payload = enc.update(plain) + enc.finalize()
        file = io.BytesIO(b'prefix' + bytes(512) + payload)
        nca = SimpleNamespace(view=Slice(file, 6, 1024), _content_key=key)
        section = Section(nca, 512, 512, header)
        self.assertEqual(section.read(13, 300), plain[13:313])
        self.assertEqual(section.read(511, 1), plain[511:])
        with self.assertRaises(ValueError):
            section.read(500, 13)

    def test_archive_offsets_and_truncation(self):
        b = bytearray(517)
        b[:16] = struct.pack('<8sII', b'PS_FS_V1', 1, 0)
        b[16:21] = b'test\0'
        struct.pack_into('<QQ', b, 64, 5, 512)
        self.assertEqual(archive_entries(Slice(io.BytesIO(b), 0, len(b))),
                         [dict(name='test', offset=512, size=5)])
        with self.assertRaises(ValueError):
            archive_entries(Slice(io.BytesIO(b), 0, 516))
        struct.pack_into('<Q', b, 72, 0)
        with self.assertRaises(ValueError):
            archive_entries(Slice(io.BytesIO(b), 0, len(b)))

    def test_nso_hash_verification(self):
        b = bytearray(256 + 64 + 16 + 16)
        b[:4] = b'NSO0'
        struct.pack_into('<I', b, 12, 0x38)
        for i, (fo, mo, size) in enumerate([(256, 0, 64), (320, 64, 16), (336, 80, 16)]):
            struct.pack_into('<III', b, 16 + 16 * i, fo, mo, size)
        struct.pack_into('<I', b, 260, 16)
        b[272:276] = b'MOD0'
        struct.pack_into('<i', b, 276, 16)
        for i, (fo, size) in enumerate([(256, 64), (320, 16), (336, 16)]):
            b[0xa0 + i * 32:0xc0 + i * 32] = hashlib.sha256(b[fo:fo + size]).digest()
        info, image = nso_info(b)
        self.assertEqual(len(image), 96)
        self.assertEqual(info['symbols'], [])
        b[319] ^= 1
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            nso_info(b)


if __name__ == '__main__':
    unittest.main()
