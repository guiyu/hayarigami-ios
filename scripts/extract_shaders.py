"""Extract this title's contiguous embedded NVN shaders; reject other layouts."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

CONTROL_SIZE = 0x880
MAGIC = b'\x34\x12\x76\x98'


def parse_shader(image, offset):
    if offset < 0 or len(image) - offset < CONTROL_SIZE:
        raise ValueError('truncated shader control')
    control = image[offset:offset + CONTROL_SIZE]
    if struct.unpack_from('<9I', control) != (0x98761234, 1, 0x10, 0x120, 0xb, 0x878, 0, 0, 0x880):
        raise ValueError('unrecognized shader control header')
    code_size, constant_size = struct.unpack_from('<2I', control, 0x6f8)
    constant_offset, total, prefix = struct.unpack_from('<3I', control, 0x700)
    if (prefix != 0x30 or code_size < 0x50 or code_size % 8 or
            constant_offset != (prefix + code_size + 255) & ~255 or
            total != (constant_offset + constant_size + 255) & ~255):
        raise ValueError('inconsistent code/constant lengths')
    start = offset + CONTROL_SIZE
    if total > len(image) - start or image[start:start + 4] != b'\x78\x56\x34\x12':
        raise ValueError('truncated or missing contiguous shader code')
    code = bytes(image[start + prefix:start + prefix + code_size])
    stage_bits = (struct.unpack_from('<I', code)[0] >> 10) & 15
    stage = {1: 'vertex', 4: 'geometry', 5: 'fragment'}.get(stage_bits)
    selector = struct.unpack_from('<I', control, 0x714)[0]
    if stage is None or {0: 1, 1: 5, 2: 4}.get(selector) != stage_bits:
        raise ValueError('unsupported SPH stage')
    return dict(offset=offset, stage=stage, control=bytes(control), code=code,
                constants=bytes(image[start + constant_offset:start + constant_offset + constant_size]))


def extract(image):
    result = []
    offset = 0
    while (offset := image.find(MAGIC, offset)) != -1:
        result.append(parse_shader(image, offset))
        offset += 4
    if not result:
        raise ValueError('no embedded NVN shaders')
    return result


def main():
    from local_output import local_output
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--out', required=True, type=local_output)
    args = parser.parse_args()
    image = args.image.read_bytes()
    shaders = extract(image)  # Validate all before writing derived files.
    args.out.mkdir(parents=True, exist_ok=True)
    records = []
    for shader in shaders:
        key = hashlib.sha256(shader['control']).hexdigest()
        record = {k: v for k, v in shader.items() if k not in ('control', 'code', 'constants')}
        record['key'] = key
        for part in ('control', 'code', 'constants'):
            data = shader[part]
            (args.out / f'{key}.{part}.bin').write_bytes(data)
            record[part + '_size'] = len(data)
            record[part + '_sha256'] = hashlib.sha256(data).hexdigest()
        records.append(record)
    manifest = dict(image_sha256=hashlib.sha256(image).hexdigest(), shaders=records)
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Extracted {len(records)} embedded shader records; not a rendering-equivalence claim')


if __name__ == '__main__':
    main()
