#!/usr/bin/env python3
"""Read only the necessary ranges; all output belongs in ignored .local/."""
import argparse
import collections
import hashlib
import json
import re
import struct
from pathlib import Path

import lz4.block
from nx_read import NCA, Slice, Subview, load_keys, pfs_entries, romfs_entries, safe_name
from local_output import local_output


def nso_info(data):
    if len(data) < 256 or data[:4] != b'NSO0':
        raise ValueError('not an NSO')
    flags = struct.unpack_from('<I', data, 12)[0]
    segments = [struct.unpack_from('<III', data, 16 + i * 16) for i in range(3)]
    sizes = struct.unpack_from('<III', data, 96)
    end = max(m + n for _, m, n in segments)
    if end > 512 * 1024 * 1024:
        raise ValueError('NSO image exceeds inspection limit')
    image = bytearray(end)
    hashes = []
    for i, (fo, mo, size) in enumerate(segments):
        stored = sizes[i] if flags >> i & 1 else size
        if fo + stored > len(data):
            raise ValueError('truncated NSO segment')
        raw = data[fo:fo + stored]
        if flags >> i & 1:
            raw = lz4.block.decompress(raw, uncompressed_size=size)
        if len(raw) != size:
            raise ValueError('NSO decompressed size mismatch')
        digest = hashlib.sha256(raw).digest()
        if flags >> (i + 3) & 1 and digest != data[0xa0 + i * 32:0xc0 + i * 32]:
            raise ValueError('NSO segment hash mismatch')
        hashes.append(dict(sha256=digest.hex(), hash_present=bool(flags >> (i + 3) & 1)))
        image[mo:mo + size] = raw
    def checked(offset, size, label):
        if offset < 0 or size < 0 or offset > len(image) or size > len(image) - offset:
            raise ValueError(label + ' outside image bounds')
        return image[offset:offset + size]

    checked(0, 8, 'NSO entry')
    mod = struct.unpack_from('<I', image, 4)[0]
    header = checked(mod, 8, 'MOD0')
    if header[:4] != b'MOD0':
        raise ValueError('NSO MOD0 not found')
    dyn = mod + struct.unpack_from('<i', header, 4)[0]
    checked(dyn, 16, 'dynamic table')
    tags = {}
    for off in range(dyn, len(image) - 15, 16):
        key, value = struct.unpack_from('<QQ', image, off)
        if not key:
            break
        tags[key] = value
    else:
        raise ValueError('unterminated dynamic table')
    symbols = []
    if 6 in tags and 5 in tags:
        if tags.get(11, 24) != 24:
            raise ValueError('not ELF64 dynamic symbols')
        count = struct.unpack_from('<I', checked(tags[4], 8, 'symbol hash'), 4)[0] if 4 in tags else (tags[5] - tags[6]) // 24
        if not 0 <= count <= 1000000:
            raise ValueError('invalid symbol count')
        if 10 not in tags:
            raise ValueError('missing string table size')
        checked(tags[6], count * 24, 'symbol table')
        strings = checked(tags[5], tags[10], 'string table')
        for i in range(count):
            name, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', image, tags[6] + i * 24)
            stop = strings.find(0, name)
            if stop < 0:
                raise ValueError('invalid dynamic symbol name')
            text = strings[name:stop].decode('utf-8')
            if text:
                symbols.append(dict(name=text, defined=bool(shndx), value=value, size=size, type=info & 15,
                                    binding=info >> 4, visibility=other & 3, section=shndx))
    relocations = collections.Counter()
    for offset_tag, size_tag in [(7, 8), (23, 2)]:
        if offset_tag in tags:
            size = tags.get(size_tag, 0)
            if size % 24:
                raise ValueError('invalid relocation table size')
            checked(tags[offset_tag], size, 'relocation table')
            for off in range(tags[offset_tag], tags[offset_tag] + tags.get(size_tag, 0), 24):
                _, info, _ = struct.unpack_from('<QQq', image, off)
                relocations[str(info & 0xffffffff)] += 1
    strings = [x.decode('ascii') for x in re.findall(rb'[ -~]{6,}', image)]
    return dict(sha256=hashlib.sha256(data).hexdigest(), build_id=data[64:96].hex(),
                segments=segments, segment_hashes=hashes, symbols=symbols,
                relocations=dict(relocations), dynamic_tags={str(k): v for k, v in tags.items()},
                middleware=[s for s in strings if s.startswith('SDK MW+')],
                nvn_names=sorted(set(s for s in strings if re.fullmatch(r'nvn[A-Z]\w+', s)))), image


def archive_entries(view):
    header = view.read(0, 16)
    if header[:8] != b'PS_FS_V1':
        raise ValueError('unsupported game archive')
    count = struct.unpack_from('<I', header, 8)[0]
    if count > 100000:
        raise ValueError('invalid archive count')
    table = view.read(16, count * 64)
    entries = []
    for i in range(count):
        row = table[i * 64:(i + 1) * 64]
        if b'\0' not in row[:48]:
            raise ValueError('unterminated archive filename')
        name = safe_name(row[:48].split(b'\0')[0].decode('utf-8'))
        size, off = struct.unpack_from('<QQ', row, 48)
        if off < 16 + count * 64 or off + size > view.size:
            raise ValueError('archive member exceeds bounds')
        entries.append(dict(name=name, offset=off, size=size))
    return entries


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--nsp', type=Path, required=True)
    ap.add_argument('--keys', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True, help='new ignored output directory')
    args = ap.parse_args()
    args.out = local_output(args.out)
    args.out.mkdir(parents=True, exist_ok=False)
    keys = load_keys(args.keys)
    report = dict(package_size=args.nsp.stat().st_size, ncas=[], modules={}, resources=[])
    with args.nsp.open('rb') as f:
        outer = Slice(f, 0, report['package_size'])
        for entry in pfs_entries(outer):
            nca = NCA(Slice(f, entry['offset'], entry['size']), keys)
            report['ncas'].append(dict(name=entry['name'], size=entry['size'], title_id=nca.title_id,
                                      type=nca.content_type, sdk_header=nca.sdk,
                                      fixed_signature_zero=nca.signature_zero))
            if nca.title_id != '010095B01AF94000':
                raise ValueError('unexpected title ID; do not apply this title-specific workflow')
            for section in nca.sections:
                fs = section.filesystem()
                entries = pfs_entries(fs) if section.kind == 1 else romfs_entries(fs)
                for e in entries:
                    if nca.content_type == 0 and section.kind == 1:
                        data = fs.read(e['offset'], e['size'])
                        dest = args.out / 'exefs' / e['name']
                        dest.parent.mkdir(exist_ok=True)
                        dest.write_bytes(data)
                        if data[:4] == b'NSO0':
                            info, image = nso_info(data)
                            report['modules'][e['name']] = info
                            (args.out / (e['name'] + '.bin')).write_bytes(image)
                        elif e['name'] == 'main.npdm':
                            report['npdm'] = dict(flags=data[12], is_64_bit=bool(data[12] & 1))
                    elif nca.content_type == 0:
                        view = Subview(fs, e['offset'], e['size'])
                        record = dict(e)
                        if view.read(0, 8) == b'PS_FS_V1':
                            record['archive'] = archive_entries(view)
                            for member in record['archive']:
                                member['prefix_hex'] = view.read(member['offset'], min(32, member['size'])).hex()
                        if e['name'] == 'package_tex.dat':
                            (args.out / 'package_tex.dat').write_bytes(view.read(0, view.size))
                        report['resources'].append(record)
                    elif nca.content_type == 2 and e['name'] == 'control.nacp':
                        data = fs.read(e['offset'], e['size'])
                        report['title'] = data[2 * 0x300:2 * 0x300 + 0x200].split(b'\0')[0].decode('utf-8')
                        report['display_version'] = data[0x3060:0x3070].split(b'\0')[0].decode('utf-8')
                    elif nca.content_type == 1:
                        data = fs.read(e['offset'], e['size'])
                        report['cnmt'] = dict(title_id=f'{struct.unpack_from("<Q", data)[0]:016X}',
                                              version=struct.unpack_from('<I', data, 8)[0], type=data[12])
    (args.out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
    print(json.dumps(dict(title=report.get('title'), version=report.get('display_version'),
                          modules=list(report['modules']), resources=len(report['resources'])), ensure_ascii=False))


if __name__ == '__main__':
    main()
