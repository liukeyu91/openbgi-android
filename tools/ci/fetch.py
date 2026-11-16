#!/usr/bin/env python3
"""Fetch locked archives; verify cached files as well as fresh downloads."""
import hashlib, json, os, subprocess, sys
from pathlib import Path
lock=json.loads(Path(__file__).with_name('dependencies.json').read_text())
out=Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
selected=sys.argv[2:] or list(lock)
for name in selected:
    spec=lock[name]
    dest=out/spec['url'].rsplit('/',1)[1]
    if not dest.exists():
        tmp=dest.with_suffix(dest.suffix+'.download')
        subprocess.run(['curl','--fail','--location','--retry','3','--proto','=https','--tlsv1.2','-o',str(tmp),spec['url']],check=True)
        if hashlib.file_digest(tmp.open('rb'),'sha256').hexdigest()!=spec['sha256']:
            tmp.unlink(); raise SystemExit('Downloaded archive checksum mismatch: '+dest.name)
        os.replace(tmp,dest)
    if hashlib.file_digest(dest.open('rb'),'sha256').hexdigest()!=spec['sha256']:
        raise SystemExit('Cached archive checksum mismatch: '+dest.name)
    print(dest.name+': verified')
