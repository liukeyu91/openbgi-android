#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Resolve v2 crash addresses with the matching release ELF and debug file.

python3 tools/android/symbolize-crash.py REPORT.txt libopenbgi.so libopenbgi.so.debug \
    --addr2line /path/to/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-addr2line
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess

def elf(path):
    data=path.read_bytes()
    if data[:6]!=b'\x7fELF\x02\x01':raise ValueError('Expected a little-endian ELF64 file')
    segments=[];build_id=None
    phoff=struct.unpack_from('<Q',data,32)[0]
    phsize,phcount=struct.unpack_from('<HH',data,54)
    for i in range(phcount):
        kind,flags,_,address,_,_,size,_=struct.unpack_from('<IIQQQQQQ',data,phoff+i*phsize)
        if kind==1 and flags&1:segments.append((address,address+size))
    shoff=struct.unpack_from('<Q',data,40)[0]
    shsize,shcount=struct.unpack_from('<HH',data,58)
    for i in range(shcount):
        _,kind,_,_,offset,size,_,_,_,_=struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*shsize)
        if kind!=7:continue
        end=offset+size
        if end>len(data):raise ValueError('Truncated ELF notes')
        while offset+12<=end:
            names,desc,kind=struct.unpack_from('<III',data,offset);offset+=12
            name=data[offset:offset+names];offset+=(names+3)&~3
            value=data[offset:offset+desc];offset+=(desc+3)&~3
            if offset>end:raise ValueError('Invalid ELF note')
            if name==b'GNU\0' and kind==3:build_id=value.hex()
    return data,segments,build_id,struct.unpack_from('<H',data,18)[0]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('report',type=Path);p.add_argument('library',type=Path);p.add_argument('symbols',type=Path)
    p.add_argument('--addr2line',default='llvm-addr2line');args=p.parse_args()
    try:
        text=args.report.read_text(encoding='utf-8',errors='replace')
        data,segments,build_id,machine=elf(args.library)
        _,_,debug_id,_=elf(args.symbols)
        expected=re.search(r'^Native SHA256: ([0-9a-f]{64})$',text,re.M)
        if not expected or expected[1]!=hashlib.sha256(data).hexdigest():
            raise ValueError('The native SHA256 is absent or does not match this library')
        if not build_id or build_id!=debug_id:raise ValueError('ELF/debug build IDs differ or are missing')
        bias=re.search(r'^Engine ELF load bias: 0x([0-9a-f]+)$',text,re.M)
        if not bias:raise ValueError('The report has no engine load bias')
        bias=int(bias[1],16);seen=set();addresses=[]
        for kind,raw in re.findall(r'^(pc|lr candidate|return candidate): 0x([0-9a-f]+)$',text,re.M):
            address=int(raw,16)-bias
            if kind!='pc':address-=4 if machine==183 else 1
            if address not in seen and any(a<=address<b for a,b in segments):
                seen.add(address);addresses.append((kind,address))
        print('Verified native SHA256 and debug build ID:',build_id)
        if not addresses:print('No recorded frame candidate lies in the engine; inspect the fault PC and maps for an external library.')
        for kind,address in addresses:
            print(f'{kind} +0x{address:x}:',flush=True)
            subprocess.run([args.addr2line,'-C','-f','-i','-e',str(args.symbols),hex(address)],check=True)
    except (OSError,ValueError,struct.error,subprocess.CalledProcessError) as error:p.exit(1,str(error)+'\n')

if __name__=='__main__':main()
