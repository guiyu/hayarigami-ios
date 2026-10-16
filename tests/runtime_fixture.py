"""Generate a tiny synthetic rewritten NSO image; contains no game content."""
import struct
from pathlib import Path


def generate(root):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=False)
    image = bytearray(0x10000)
    # Load a RELATIVE-relocated pointer and return its pointed-to value.
    # adrp x0,0x4000; ldr x0,[x0,#0x400]; ldr x0,[x0]; ret.
    struct.pack_into('<4I', image, 0x10, 0x90000020, 0xf9420000, 0xf9400000, 0xd65f03c0)
    struct.pack_into('<I', image, 4, 0x20)
    image[0x20:0x24] = b'MOD0'
    struct.pack_into('<i', image, 0x24, 0x4000 - 0x20)
    for i, pair in enumerate([(6, 0x4100), (5, 0x4200), (10, 12), (4, 0x4300), (11, 24), (7, 0x4600), (8, 24), (0, 0)]):
        struct.pack_into('<QQ', image, 0x4000 + 16 * i, *pair)
    image[0x4200:0x420c] = b'\0hy_fixture\0'
    struct.pack_into('<IBBHQQ', image, 0x4118, 1, 0x12, 0, 1, 0x10, 16)
    struct.pack_into('<II', image, 0x4300, 1, 2)
    struct.pack_into('<Q', image, 0x4500, 42)
    struct.pack_into('<QQq', image, 0x4600, 0x4400, 1027, 0x4500)
    (root / 'fixture.patched.bin').write_bytes(image)
    (root / 'fixture.fixups.tsv').write_text(
        '# TEXT_END\t0x100\n# PATCH_OFF\t0xc000\n# KEY_PAGE_OFF\t0x8000\n'
        '# KEY_PAGE_SIZE\t0x4000\n# RO_OFF\t0x4000\n# DATA_OFF\t0x5000\n')
    (root / 'fixture.gates.tsv').write_text('')


if __name__ == '__main__':
    import sys
    generate(sys.argv[1])
