#!/usr/bin/env python3
# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only
"""Share declarative resource profiles with the Android importer."""
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1] / 'translation_compat/profiles'
# Complete resource profiles precede narrower proofs. Standalone update packs
# are handled by the generic container discovery after the primary profile.
names = ['kanojostep-evb.json', 'makinghd-evb.json', 'makinghd-cpk.json',
         'friend-to-lover-opening.json']
quote = lambda s: json.dumps(s, ensure_ascii=True)
rows = []
for name in names:
    p = json.loads((root / name).read_text(encoding='utf-8'))
    sources = []
    for s in p['sources']:
        arr = lambda x: 'new String[]{' + ','.join(map(quote, x)) + '}'
        sources.append('new PatchImport.Source(%s,%s,%s,%s,%s)' % (
            quote(s['format']), quote(s['path']), quote(s.get('key', '')),
            arr(s['select']), arr(s.get('exclude', []))))
    r = p.get('runtime', {})
    rows.append('new PatchImport.Profile(%s,%s,%s,%s,new PatchImport.Source[]{%s})' % (
        quote(p['id']), quote(r.get('engine', '')), quote(r.get('text_encoding', '')),
        quote(r.get('font_family', '')),
        ','.join(sources)))
text = ('/* Generated from shared JSON profiles; SPDX-License-Identifier: GPL-2.0-only */\n'
        'package org.openbgi.port;\nfinal class PatchProfiles {\n'
        'static final PatchImport.Profile[] ALL={\n' + ',\n'.join(rows) + '\n};\n}\n')
Path(sys.argv[1]).write_text(text, encoding='utf-8')
