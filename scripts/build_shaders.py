"""Translate extracted shaders with audited local dependencies and compile Metal libraries."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from local_output import local_output


def run(command, log):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log.write(result.stdout)
    if result.returncode:
        raise RuntimeError(f'{Path(command[0]).name} failed ({result.returncode}); see build.log')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--dependencies', required=True, type=Path)
    parser.add_argument('--out', required=True, type=local_output)
    parser.add_argument('--key', action='append')
    parser.add_argument('--alpha-test', choices=['always', 'greater-zero'], default='always')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = args.source.resolve()
    manifest = json.loads((source / 'manifest.json').read_text())
    records = manifest['shaders']
    if args.key:
        records = [r for r in records if r['key'] in args.key]
        if {r['key'] for r in records} != set(args.key):
            raise ValueError('requested shader key missing')
    dependencies = args.dependencies.resolve()
    required = ['Ryujinx.Common.dll', 'Ryujinx.Graphics.Shader.dll', 'Spv.Generator.dll']
    provenance = {name: hashlib.sha256((dependencies / name).read_bytes()).hexdigest() for name in required}
    args.out.mkdir(parents=True, exist_ok=True)
    result = dict(image_sha256=manifest['image_sha256'], dependencies=provenance, built=[], failed=[])
    with (args.out / 'build.log').open('w') as log:
        run(['dotnet', 'build', str(root / 'tools/shader_xlate/ShaderXlate.csproj'),
             f'-p:ShaderDependencies={dependencies}', f'-p:BaseIntermediateOutputPath={args.out}/obj/',
             '-o', str(args.out / 'translator'), '--nologo'], log)
        for record in records:
            key = record['key']
            try:
                for part in ('control', 'code', 'constants'):
                    data = (source / f'{key}.{part}.bin').read_bytes()
                    if hashlib.sha256(data).hexdigest() != record[part + '_sha256']:
                        raise ValueError(f'{part} hash mismatch')
                base = args.out / key
                run(['dotnet', str(args.out / 'translator/ShaderXlate.dll'),
                     str(source / f'{key}.code.bin'), str(source / f'{key}.constants.bin'), str(base), record['stage'], args.alpha_test], log)
                run(['spirv-cross', str(base) + '.spv', '--msl', '--msl-ios', '--msl-version', '20400',
                     '--msl-decoration-binding', '--output', str(base) + '.metal'], log)
                for sdk in ('macosx', 'iphoneos'):
                    air = str(base) + f'.{sdk}.air'
                    run(['xcrun', '--sdk', sdk, 'metal', '-fmodules-cache-path=' + str(args.out / 'module-cache'),
                         '-fno-fast-math', '-c', str(base) + '.metal', '-o', air], log)
                    run(['xcrun', '--sdk', sdk, 'metallib', air, '-o', str(base) + f'.{sdk}.metallib'], log)
                metadata_path = Path(str(base) + '.json')
                metadata = json.loads(metadata_path.read_text())
                metadata['MetalLibraries'] = {sdk: hashlib.sha256(Path(str(base) + f'.{sdk}.metallib').read_bytes()).hexdigest()
                                              for sdk in ('macosx', 'iphoneos')}
                metadata_path.write_text(json.dumps(metadata, indent=2) + '\n')
                result['built'].append(record)
                print(f'Built {record["stage"]} {key[:16]}', flush=True)
            except (RuntimeError, ValueError) as error:
                result['failed'].append(dict(key=key, error=str(error)))
                print(f'Failed {record["stage"]} {key[:16]}: {error}', flush=True)
    (args.out / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'{len(result["built"])} built, {len(result["failed"])} failed; draw-state validation still required')
    return 1 if result['failed'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
