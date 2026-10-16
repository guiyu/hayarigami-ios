#!/usr/bin/env python3
"""Static comparison only: a name match does not establish ABI/behavior compatibility."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from local_output import local_output


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--analysis', type=Path, required=True)
    ap.add_argument('--reference', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    a.out = local_output(a.out)
    r = json.loads(a.analysis.read_text())
    dm = a.reference / 'dm'
    force = set()
    for p in (dm / 'host').glob('hle*.c'):
        force.update(re.findall(r'HLE_FORCE\(\s*"([^"]+)"', p.read_text()))
    imports = [s['name'] for s in r['modules']['main']['symbols'] if not s['defined']]
    nn = set(s for s in imports if s.startswith('_ZN2nn'))
    old_nvn = set((dm / 'gfx/nvn/names.txt').read_text().split())
    symbols = [s for m in r['modules'].values() for s in m['symbols']]
    definitions = {s['name'] for s in symbols if s['defined']}
    unresolved = sorted({s['name'] for s in symbols if not s['defined']} - definitions)
    result = dict(reference_commit=subprocess.check_output(
        ['git', '-C', str(a.reference), 'rev-parse', 'HEAD'], text=True).strip(),
        main_nn_imports=len(nn), direct_force_name_matches=len(nn & force),
        not_directly_forced=sorted(nn - force),
        nvn_strings_new=sorted(set(r['modules']['main']['nvn_names']) - old_nvn),
        imports_without_module_definition=unresolved,
        caveat='Static name comparison only; SDK-defined functions may still need Horizon services. NVN names do not prove runtime use.',
        tools={str(p.relative_to(a.reference)): hashlib.sha256(p.read_bytes()).hexdigest()
               for p in [dm / 'tools/patch_nso.py', dm / 'tools/guest_dylib.py',
                         dm / 'verify/structck.py', dm / 'verify/equiv.py', dm / 'verify/nsoimg.py']})
    with a.out.open('x') as f:
        json.dump(result, f, indent=2)
    print(json.dumps({k: result[k] for k in ['main_nn_imports', 'direct_force_name_matches']}))


if __name__ == '__main__':
    main()
