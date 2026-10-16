#!/usr/bin/env python3
"""Extract this title's RomFS once, streaming from the original local NSP."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
from local_output import local_output
from nx_read import NCA, Slice, load_keys, pfs_entries, romfs_entries


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--nsp', type=Path, required=True)
    ap.add_argument('--keys', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    out = local_output(args.out)
    out.mkdir(parents=True, exist_ok=False)
    keys = load_keys(args.keys)
    manifest = []
    with args.nsp.open('rb') as f:
        for entry in pfs_entries(Slice(f, 0, args.nsp.stat().st_size)):
            nca = NCA(Slice(f, entry['offset'], entry['size']), keys)
            if nca.title_id != '010095B01AF94000':
                raise ValueError('wrong title')
            if nca.content_type != 0:
                continue
            for section in nca.sections:
                if section.kind != 0:
                    continue
                fs = section.filesystem()
                entries = romfs_entries(fs)
                if shutil.disk_usage(out).free < sum(e['size'] for e in entries) + 2 * 1024**3:
                    raise ValueError('insufficient space for extraction plus 2 GiB reserve')
                for e in entries:
                    dest = out / e['name']
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    digest = hashlib.sha256()
                    with dest.open('xb') as target:
                        for offset in range(0, e['size'], 8 * 1024**2):
                            data = fs.read(e['offset'] + offset, min(8 * 1024**2, e['size'] - offset))
                            target.write(data)
                            digest.update(data)
                    manifest.append(dict(name=e['name'], size=e['size'], sha256=digest.hexdigest()))
                    print(f"extracted {len(manifest)}/{len(entries)}: {e['name']}", flush=True)
    if not manifest:
        raise ValueError('no resources extracted')
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
