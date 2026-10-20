#!/usr/bin/env python3
"""Export an already signed local App to an IPA, with archive integrity evidence."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import subprocess
import zipfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('app',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
app=args.app.resolve(strict=True)
output=args.output.resolve()
if app.suffix!='.app' or output.suffix!='.ipa':
    parser.error('expected a signed .app directory and a new .ipa path')
if output.exists():
    parser.error('output already exists')
subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
info=plistlib.loads((app/'Info.plist').read_bytes())
profile=plistlib.loads(subprocess.check_output(['security','cms','-D','-i',str(app/'embedded.mobileprovision')],stderr=subprocess.DEVNULL))
bundle=info['CFBundleIdentifier']
if profile['Entitlements']['application-identifier'].split('.',1)[1]!=bundle:
    parser.error('provisioning profile does not match this Bundle ID')
expiry=profile['ExpirationDate'].replace(tzinfo=datetime.timezone.utc)
if expiry<=datetime.datetime.now(datetime.timezone.utc):
    parser.error('provisioning profile has expired')
output.parent.mkdir(parents=True,exist_ok=True)
temporary=output.with_suffix('.ipa.partial')
try:
    count=0
    with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=1,allowZip64=True) as archive:
        for path in sorted(app.rglob('*')):
            if path.is_symlink():
                raise ValueError('unexpected symbolic link in App')
            if path.is_file():
                archive.write(path,Path('Payload')/app.name/path.relative_to(app))
                count+=1
    with zipfile.ZipFile(temporary) as archive:
        bad=archive.testzip()
        if bad:raise ValueError('archive CRC failed: '+bad)
        archived=plistlib.loads(archive.read('Payload/'+app.name+'/Info.plist'))
        if archived!=info:raise ValueError('archived Info.plist differs')
    os.replace(temporary,output)
except BaseException:
    temporary.unlink(missing_ok=True)
    raise
with output.open('rb') as exported:
    sha=hashlib.file_digest(exported,'sha256').hexdigest()
receipt={'file':output.name,'bundle_id':bundle,'version':info['CFBundleVersion'],'files':count,'bytes':output.stat().st_size,'sha256':sha,'profile_expires_utc':expiry.isoformat(),'verification':'signed App verified and archive CRC passed; playability requires separate device evidence'}
output.with_suffix('.ipa.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
