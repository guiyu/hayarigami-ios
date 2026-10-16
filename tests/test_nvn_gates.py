import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from generate_nvn_gates import generate, names_from


class GatesTest(unittest.TestCase):
    def test_whole_names_deduplicated(self):
        self.assertEqual(names_from(b'\0nvnDeviceGetInteger\0nvnFoo\0nvnFoo\0bad_nvnNo\0nvn\0'),
                         ['nvnDeviceGetInteger', 'nvnFoo'])

    def test_static_gates_stop_with_exact_index(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            self.assertEqual(generate(b'\0nvnFoo\0nvnBar\0', out), 2)
            asm = (out / 'nvn_gates.S').read_text()
            self.assertEqual(asm.count('b _hy_nvn_unimplemented'), 2)
            self.assertIn('mov w0, #1', asm)
            self.assertIn('"nvnBar","nvnFoo"', (out / 'nvn_names.c').read_text())

    def test_empty_names_rejected(self):
        with self.assertRaises(ValueError):
            generate(b'not an NVN program', Path('/unused'))


if __name__ == '__main__':
    unittest.main()
