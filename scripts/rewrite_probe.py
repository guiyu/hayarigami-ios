#!/usr/bin/env python3
"""Run pinned read-only reference tools on this title; never build the D2R App."""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from local_output import local_output


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference', type=Path, required=True)
    ap.add_argument('--audit', type=Path, required=True)
    ap.add_argument('--analysis', type=Path, required=True)
    ap.add_argument('--llvm-bin', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True, help='new ignored output directory')
    ap.add_argument('--equivalence', action='store_true', help='test main/sdk/subsdk0 instruction rewrites (can take minutes)')
    a = ap.parse_args()
    a.out = local_output(a.out)
    audit, report = json.loads(a.audit.read_text()), json.loads((a.analysis / 'report.json').read_text())
    if report.get('cnmt', {}).get('title_id') != '010095B01AF94000':
        raise ValueError('wrong title')
    if set(report['modules']) != {'main', 'rtld', 'sdk', 'subsdk0'}:
        raise ValueError('unexpected module set; investigate before rewriting')
    for rel, expected in audit['tools'].items():
        if hashlib.sha256((a.reference / rel).read_bytes()).hexdigest() != expected:
            raise ValueError('reference tool changed; repeat audit: ' + rel)
    a.out.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith('DM_')}
    env.update(PYTHONDONTWRITEBYTECODE='1', PYTHONOPTIMIZE='0', DM_RULESET='native', DM_PRIVATE_GATES='0',
               DM_ROOT=str(a.out.resolve() / 'reference-cache'), LLVM_BIN=str(a.llvm_bin.resolve()))
    if a.equivalence:
        # A sandbox that disallows JIT kills Unicorn workers and can hang Pool.
        preflight = subprocess.run([sys.executable, '-c',
            'from unicorn import *; u=Uc(UC_ARCH_ARM64, UC_MODE_ARM); '
            'u.mem_map(65536,4096); u.mem_write(65536,bytes.fromhex("200080d2")); '
            'u.emu_start(65536,65540,count=1)'], env=env, timeout=15)
        if preflight.returncode:
            raise RuntimeError('Unicorn preflight failed; use a local environment that permits JIT')
    results = []

    def run(script, arguments, log):
        with (a.out / log).open('w') as f:
            p = subprocess.run([sys.executable, str(a.reference / script), *map(str, arguments)],
                               env=env, stdout=f, stderr=subprocess.STDOUT)
        results.append(dict(tool=script, log=log, exit_code=p.returncode))
        (a.out / 'commands.json').write_text(json.dumps(results, indent=2))
        if p.returncode:
            raise RuntimeError('probe failed; inspect local log: ' + log)

    for mod, info in report['modules'].items():
        nso = a.analysis / 'exefs' / mod
        if hashlib.sha256(nso.read_bytes()).hexdigest() != info['sha256']:
            raise ValueError('module changed after analysis: ' + mod)
        run('dm/tools/patch_nso.py', [nso, a.out, mod], mod + '-rewrite.log')
        for suffix in ['cuts', 'obs', 'params']:
            p = a.out / f'{mod}.{suffix}.tsv'
            if p.exists() and any(l.strip() and not l.startswith('#') for l in p.read_text().splitlines()):
                raise ValueError('unexpected game-specific rewrite registration')
        run('dm/verify/structck.py', [a.out, mod, nso], mod + '-structure.log')
    if a.equivalence:
        for mod in ['main', 'sdk', 'subsdk0']:
            run('dm/verify/equiv.py', [a.out, mod, a.analysis / 'exefs' / mod, '--jobs', '2',
                                     '--seed', '1', '--mutate', '50', '--tsv', a.out / (mod + '-equivalence.tsv')],
                mod + '-equivalence.log')
    # Packaging only. This does not supply a loader, services, GPU translation or signing.
    run('dm/tools/guest_dylib.py', [a.out, a.out / 'candidate', *report['modules']], 'package.log')
    print('Experimental guest container produced; not an installable or playable App.')


if __name__ == '__main__':
    main()
