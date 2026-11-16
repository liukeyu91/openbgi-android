#!/usr/bin/env python3
# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only
"""Remove only in-tree build directories, refusing legacy signing material."""
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parents[1]
targets = []
for name in sys.argv[1:]:
    path = (root / name).resolve()
    if path == root or root not in path.parents or path.parts[len(root.parts)] not in ('obj', 'bin'):
        raise SystemExit('Refusing to clean a path outside obj/ or bin/: ' + str(path))
    if path.exists():
        if path.is_file():
            raise SystemExit('Build directory is a file: ' + str(path))
        for child in path.rglob('*'):
            if child.suffix.lower() in ('.p12', '.pfx', '.jks', '.keystore'):
                raise SystemExit('Refusing to delete signing material: ' + str(child) + '\nMigrate and verify the key outside the checkout first.')
    targets.append(path)
for path in targets:
    if path.exists():
        shutil.rmtree(path)
