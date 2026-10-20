#!/usr/bin/env python3
"""Prepare a private Xcode project using locally verified game artifacts only."""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--modules', type=Path, required=True)
p.add_argument('--romfs', type=Path, required=True)
p.add_argument('--shaders', type=Path, required=True)
a = p.parse_args()
modules, romfs, shaders = (x.resolve(strict=True) for x in (a.modules, a.romfs, a.shaders))
if shutil.disk_usage('/private/tmp').free < 8 * 1024**3:
    p.error('at least 8 GiB free space is required for the App and IPA')
stage = Path(tempfile.mkdtemp(prefix='hayarigami-ios-', dir='/private/tmp'))
os.chmod(stage, 0o700)
resources = stage / 'Resources'
(resources / 'Modules').mkdir(parents=True)
for name in ('main', 'subsdk0', 'sdk'):
    for suffix in ('fixups.tsv', 'gates.tsv'):
        shutil.copyfile(modules / f'{name}.{suffix}', resources / 'Modules' / f'{name}.{suffix}')
shutil.copytree(romfs, resources / 'RomFS', copy_function=os.link, symlinks=False)
(resources / 'Shaders').mkdir()
for variant in ('', 'alpha5-ref0'):
    source = shaders / variant
    dest = resources / 'Shaders' / variant
    dest.mkdir(exist_ok=True)
    for meta in sorted(source.glob('*.json')):
        library = meta.with_suffix('.iphoneos.metallib')
        if library.is_file():
            shutil.copyfile(meta, dest / meta.name)
            shutil.copyfile(library, dest / library.name)
framework = stage / 'Guest.framework'
shutil.copytree(modules / 'candidate' / 'Guest.framework', framework,
                copy_function=shutil.copyfile, ignore=shutil.ignore_patterns('_CodeSignature'))
os.chmod(framework / 'Guest', 0o755)
sources = []
for directory in ('runtime', 'gfx', 'audio', 'media', 'ios'):
    for path in sorted((ROOT / 'src' / directory).iterdir()):
        if path.suffix in ('.c', '.m', '.S') and path != ROOT / 'src/runtime/main.c':
            sources.append({'path': str(path)})
for name in ('nvn_names.c', 'nvn_gates.S'):
    sources.append({'path': str(ROOT / '.local/gfx-generated' / name)})
for name in ('Modules', 'RomFS', 'Shaders'):
    sources.append({'path': str(resources / name), 'type': 'folder', 'buildPhase': 'resources'})
settings = {
    'PRODUCT_BUNDLE_IDENTIFIER': 'com.guiyu.hayarigami', 'PRODUCT_NAME': 'Hayarigami',
    'MARKETING_VERSION': '1.0', 'CURRENT_PROJECT_VERSION': '1',
    'ARCHS': 'arm64', 'CLANG_ENABLE_OBJC_ARC': 'YES', 'GCC_C_LANGUAGE_STANDARD': 'c11',
    'OTHER_CFLAGS': ['-Wall', '-Wextra', '-Werror'], 'CODE_SIGN_STYLE': 'Automatic',
    'CLANG_WARN_COMMA': 'NO', 'TARGETED_DEVICE_FAMILY': '1', 'ENABLE_BITCODE': 'NO',
}
info = {'CFBundleDisplayName': '流行之神', 'CFBundleVersion': '$(CURRENT_PROJECT_VERSION)',
        'CFBundleShortVersionString': '$(MARKETING_VERSION)', 'UILaunchScreen': {},
        'UIRequiresFullScreen': True, 'UIFileSharingEnabled': True,
        'UISupportedInterfaceOrientations': ['UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'],
        'UIApplicationSceneManifest': {'UIApplicationSupportsMultipleScenes': False}}
spec = {'name': 'Hayarigami', 'options': {'deploymentTarget': {'iOS': '26.0'}},
        'targets': {'Hayarigami': {'type': 'application', 'platform': 'iOS',
          'sources': sources, 'settings': {'base': settings},
          'info': {'path': str(stage / 'Info.plist'), 'properties': info},
          'dependencies': [{'framework': str(framework), 'embed': True, 'codeSign': True}] +
                          [{'sdk': x + '.framework'} for x in ('UIKit', 'Metal', 'QuartzCore', 'AudioToolbox', 'Foundation', 'VideoToolbox', 'CoreMedia', 'CoreVideo')]}}}
(stage / 'project.json').write_text(json.dumps(spec, indent=2))
subprocess.run(['xcodegen', 'generate', '--spec', str(stage / 'project.json'), '--project', str(stage)], check=True)
print(stage)
