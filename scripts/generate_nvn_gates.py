#!/usr/bin/env python3
"""Generate static fail-fast NVN call gates from this local program's API names."""
import argparse
import hashlib
import json
import re
from pathlib import Path
from local_output import local_output


def names_from(data):
    return sorted({m.decode('ascii') for m in re.findall(rb'(?<![A-Za-z0-9_])nvn[A-Z][A-Za-z0-9]{1,100}(?=\x00)', data)})


def generate(data, out):
    names = names_from(data)
    if not names or len(names) > 2048:
        raise ValueError('invalid NVN name count')
    out.mkdir(parents=True, exist_ok=True)
    c = ['#include <stddef.h>']
    asm = ['.text']
    for i in range(len(names)):
        c.append(f'extern void hy_nvn_gate_{i}(void);')
        asm += [f'.globl _hy_nvn_gate_{i}', '.p2align 2', f'_hy_nvn_gate_{i}:', f'    mov w0, #{i}', '    b _hy_nvn_unimplemented']
    c += ['const char *const hy_nvn_names[] = {' + ','.join(json.dumps(n) for n in names) + '};',
          'void *const hy_nvn_gates[] = {' + ','.join(f'(void *)hy_nvn_gate_{i}' for i in range(len(names))) + '};',
          f'const size_t hy_nvn_name_count = {len(names)};']
    (out / 'nvn_names.c').write_text('\n'.join(c) + '\n')
    (out / 'nvn_gates.S').write_text('\n'.join(asm) + '\n')
    (out / 'manifest.json').write_text(json.dumps(dict(source_sha256=hashlib.sha256(data).hexdigest(), count=len(names)), indent=2))
    return len(names)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--program', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    print(f'Generated {generate(args.program.read_bytes(), local_output(args.out))} static NVN gates')
