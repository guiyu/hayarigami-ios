import io
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from nx_read import Slice, pfs_entries
from local_output import local_output


def fixture(name=b'main\0', payload=b'abc'):
    return (struct.pack('<4sIII', b'PFS0', 1, len(name), 0)
            + struct.pack('<QQII', 0, len(payload), 0, 0) + name + payload)


class ContainerTests(unittest.TestCase):
    def test_output_cannot_enter_source_or_reference(self):
        root = Path(__file__).resolve().parents[1]
        self.assertEqual(local_output(root / '.local/run'), root / '.local/run')
        for path in [root / 'src/exefs', root / '.local/../tracked', Path('/tmp/exefs')]:
            with self.assertRaises(ValueError):
                local_output(path)

    def test_nonzero_slice_offset(self):
        b = fixture()
        view = Slice(io.BytesIO(b'prefix' + b + b'trailing'), 6, len(b))
        entry, = pfs_entries(view)
        self.assertEqual(entry['name'], 'main')
        self.assertEqual(view.read(entry['offset'], entry['size']), b'abc')

    def test_range_cannot_escape_slice(self):
        view = Slice(io.BytesIO(b'abcdef'), 1, 3)
        for off, size in [(-1, 1), (0, -1), (2, 2)]:
            with self.assertRaises(ValueError):
                view.read(off, size)

    def test_short_read(self):
        with self.assertRaises(ValueError):
            Slice(io.BytesIO(b'a'), 0, 2).read(0, 2)

    def test_bad_magic(self):
        b = b'BAD!' + fixture()[4:]
        with self.assertRaises(ValueError):
            pfs_entries(Slice(io.BytesIO(b), 0, len(b)))

    def test_truncated_payload(self):
        b = fixture()[:-1]
        with self.assertRaises(ValueError):
            pfs_entries(Slice(io.BytesIO(b), 0, len(b)))

    def test_unsafe_path(self):
        b = fixture(b'../outside\0')
        with self.assertRaises(ValueError):
            pfs_entries(Slice(io.BytesIO(b), 0, len(b)))


if __name__ == '__main__':
    unittest.main()
