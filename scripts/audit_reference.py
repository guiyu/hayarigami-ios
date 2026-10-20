#!/usr/bin/env python3
"""Static comparison only: a name match does not establish ABI/behavior compatibility."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from local_output import local_output


def import_summary(report):
    symbols = [s for module in report['modules'].values() for s in module['symbols']]
    definitions = {s['name'] for s in symbols if s['defined']}
    imports = [s for s in symbols if not s['defined'] and s['name'] not in definitions]
    return dict(imports_without_module_definition=sorted({s['name'] for s in imports}),
                unresolved_strong_imports=sorted({s['name'] for s in imports if s.get('binding') == 1}),
                unresolved_weak_imports=sorted({s['name'] for s in imports if s.get('binding') == 2}),
                unresolved_unknown_binding_imports=sorted({s['name'] for s in imports if s.get('binding') not in (1, 2)}))


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
    summary = import_summary(r)
    result = dict(reference_commit=subprocess.check_output(
        ['git', '-C', str(a.reference), 'rev-parse', 'HEAD'], text=True).strip(),
        main_nn_imports=len(nn), direct_force_name_matches=len(nn & force),
        not_directly_forced=sorted(nn - force),
        nvn_strings_new=sorted(set(r['modules']['main']['nvn_names']) - old_nvn),
        **summary,
        caveat='Static name comparison only; SDK-defined functions may still need Horizon services. NVN names do not prove runtime use.',
        tools={str(p.relative_to(a.reference)): hashlib.sha256(p.read_bytes()).hexdigest()
               for p in [dm / 'tools/patch_nso.py', dm / 'tools/guest_dylib.py',
                         dm / 'verify/structck.py', dm / 'verify/equiv.py', dm / 'verify/nsoimg.py']})
    with a.out.open('x') as f:
        json.dump(result, f, indent=2)
    print(json.dumps({k: result[k] for k in ['main_nn_imports', 'direct_force_name_matches']}))


if __name__ == '__main__':
    main()
