#!/usr/bin/env python3
"""Collect only build logs, hashes and unsigned APKs, preserving relative paths."""
from pathlib import Path
import shutil, sys
source=Path(sys.argv[1]).resolve(); output=Path(sys.argv[2]).resolve()
for pattern in ('build.log','reproducibility.json','openbgi-unsigned.apk'):
    for file in source.rglob(pattern):
        target=output/file.relative_to(source)
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(file,target)
