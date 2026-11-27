#!/usr/bin/env python3
"""Build one Git commit in two independent paths and compare native/APK bytes."""
import hashlib, json, os, shutil, subprocess, sys, zipfile
from pathlib import Path
source=Path(__file__).resolve().parents[2]
work=Path(sys.argv[1]).resolve(); deps=Path(sys.argv[2]).resolve()
work.mkdir(parents=True,exist_ok=True)
env=dict(os.environ)
env['SOURCE_DATE_EPOCH']=subprocess.check_output(['git','-c','safe.directory='+str(source),'-C',str(source),'log','-1','--format=%ct'],text=True).strip()
files=subprocess.check_output(['git','-c','safe.directory='+str(source),'-C',str(source),'ls-files','-z']).decode().split('\0')
results=[]
for name in ('first','a-different-length/second'):
    dest=work/name
    if dest.exists(): raise SystemExit('Use an empty reproduction directory: '+str(dest))
    dest.mkdir(parents=True)
    for rel in filter(None,files):
        target=dest/rel; target.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(source/rel,target)
    with (dest/'build.log').open('w') as log:
        subprocess.run(['sh',str(source/'tools/android/prepare-upstream.sh'),str(dest/'upstream-core')],
                       stdout=log,stderr=subprocess.STDOUT,check=True)
        subprocess.run(['sh',str(dest/'tools/ci/android-build.sh'),str(deps)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    apk=dest/'bin/android-full/openbgi-unsigned.apk'
    saf_apk=dest/'bin/android-full/openbgi-saf-unsigned.apk'
    so=dest/'bin/android/arm64-v8a/libopenbgi.so'
    ui_apk=dest/'bin/android-ui-preview/openbgi-unsigned.apk'
    with zipfile.ZipFile(apk) as z:
        dex=z.read('classes.dex')
        dual={name:hashlib.sha256(z.read('lib/arm64-v8a/'+name)).hexdigest()
              for name in ('libopenbgi-arm64.so','libopenbgi_compare.so')}
    debug=so.with_name('libopenbgi.so.debug')
    results.append({'apk':hashlib.sha256(apk.read_bytes()).hexdigest(),'so':hashlib.sha256(so.read_bytes()).hexdigest(),'symbols':hashlib.sha256(debug.read_bytes()).hexdigest(),'dex':hashlib.sha256(dex).hexdigest(),'ui_apk':hashlib.sha256(ui_apk.read_bytes()).hexdigest(),'saf_apk':hashlib.sha256(saf_apk.read_bytes()).hexdigest(),'secondary_libraries':dual})
report={'source_date_epoch':env['SOURCE_DATE_EPOCH'],'commit':subprocess.check_output(['git','-c','safe.directory='+str(source),'-C',str(source),'rev-parse','HEAD'],text=True).strip(),'builds':results,'identical':results[0]==results[1]}
(work/'reproducibility.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
if not report['identical']: raise SystemExit('Reproduction mismatch')
