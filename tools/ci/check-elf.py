#!/usr/bin/env python3
"""Require release hardening in the actual ARM64 shared library."""
import hashlib, json, re, subprocess, sys
from pathlib import Path
elf=Path(sys.argv[1]); readelf=sys.argv[2]
def inspect(*args): return subprocess.check_output([readelf,*args,str(elf)],text=True)
headers=inspect('-lW'); dynamic=inspect('-dW'); symbols=inspect('-sW')
stack=next((line.split() for line in headers.splitlines() if line.strip().startswith('GNU_STACK')),[])
checks={
 'relro':'GNU_RELRO' in headers,
 'bind_now':'BIND_NOW' in dynamic or bool(re.search(r'FLAGS_1.*NOW',dynamic)),
 'non_executable_stack':bool(stack) and 'E' not in ''.join(stack[6:-1]),
 'stack_canary':'__stack_chk_fail' in symbols,
 'fortify':bool(re.search(r'__(?!stack_chk)[a-zA-Z0-9_]+_chk\b',symbols)),
 'no_sanitizer_runtime':not any(re.search(r'NEEDED.*'+name,dynamic,re.I) for name in ('asan','ubsan'))
}
report={'sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'checks':checks}
print(json.dumps(report,indent=2))
if not all(checks.values()): raise SystemExit('ELF hardening check failed')
