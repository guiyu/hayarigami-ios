import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('extract_shaders', Path(__file__).resolve().parents[1] / 'scripts/extract_shaders.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    data = bytearray(0xa80)
    struct.pack_into('<9I', data, 0, 0x98761234, 1, 0x10, 0x120, 0xb, 0x878, 0, 0, 0x880)
    struct.pack_into('<2I', data, 0x6f8, 0xd0, 16)
    struct.pack_into('<3I', data, 0x700, 0x100, 0x200, 0x30)
    struct.pack_into('<I', data, 0x880, 0x12345678)
    struct.pack_into('<I', data, 0x8b0, 0x610461)
    data[0x980:0x990] = bytes(range(16))
    return data


class ShaderTests(unittest.TestCase):
    def test_code_and_constants_are_separate(self):
        result = module.parse_shader(fixture(), 0)
        self.assertEqual(result['stage'], 'vertex')
        self.assertEqual(result['code'], bytes(fixture()[0x8b0:0x980]))
        self.assertEqual(result['constants'], bytes(range(16)))

    def test_truncated_or_inconsistent_rejected(self):
        for field, value in [(0x20, 0xfffffff0), (0x700, 0x101), (0x704, 0xfffffff0), (0x708, 0x28), (0x6f8, 0x100), (0x6fc, 0x101), (0x714, 1), (0x880, 0)]:
            data = fixture()
            struct.pack_into('<I', data, field, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                module.parse_shader(data, 0)
        with self.assertRaises(ValueError):
            module.parse_shader(fixture()[:-1], 0)

    def test_magic_and_stage_rejected(self):
        for offset, value in [(0, 0), (0x8b0, 0)]:
            data = fixture()
            struct.pack_into('<I', data, offset, value)
            with self.assertRaises(ValueError):
                module.parse_shader(data, 0)


if __name__ == '__main__':
    unittest.main()
