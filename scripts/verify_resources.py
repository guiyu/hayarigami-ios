#!/usr/bin/env python3
"""Compare the native parser against every real PS_FS_V1 table without copying assets."""
import argparse
import ctypes
import json
from pathlib import Path
from analyze import archive_entries
from nx_read import NCA, Slice, Subview, load_keys, pfs_entries, romfs_entries


class Entry(ctypes.Structure):
    _fields_ = [('name', ctypes.c_char * 49), ('offset', ctypes.c_uint64), ('size', ctypes.c_uint64)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--nsp', type=Path, required=True)
    ap.add_argument('--keys', type=Path, required=True)
    ap.add_argument('--library', type=Path, required=True)
    args = ap.parse_args()
    lib = ctypes.CDLL(str(args.library.resolve()))
    lib.psfs_get.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint64, ctypes.c_uint32, ctypes.POINTER(Entry)]
    lib.psfs_get.restype = ctypes.c_int
    keys = load_keys(args.keys)
    archive_count = member_count = 0
    with args.nsp.open('rb') as f:
        for e in pfs_entries(Slice(f, 0, args.nsp.stat().st_size)):
            nca = NCA(Slice(f, e['offset'], e['size']), keys)
            if nca.title_id != '010095B01AF94000':
                raise ValueError('unexpected title ID')
            if nca.content_type != 0:
                continue
            for section in nca.sections:
                if section.kind != 0:
                    continue
                fs = section.filesystem()
                for item in romfs_entries(fs):
                    view = Subview(fs, item['offset'], item['size'])
                    if view.read(0, 8) != b'PS_FS_V1':
                        continue
                    entries = archive_entries(view)
                    table = view.read(0, 16 + 64 * len(entries))
                    buffer = ctypes.create_string_buffer(table)
                    for i, expected in enumerate(entries):
                        actual = Entry()
                        if lib.psfs_get(buffer, len(table), view.size, i, ctypes.byref(actual)):
                            raise ValueError('native parser rejected a real archive entry')
                        if dict(name=actual.name.decode(), offset=actual.offset, size=actual.size) != expected:
                            raise ValueError('native and Python archive parsers disagree')
                    archive_count += 1
                    member_count += len(entries)
    if archive_count == 0 or member_count == 0:
        raise ValueError('no game archives verified; zero coverage is not success')
    print(json.dumps(dict(archives=archive_count, members=member_count, all_entries_match=True)))


if __name__ == '__main__':
    main()
