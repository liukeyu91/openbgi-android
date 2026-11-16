#!/usr/bin/env python3
# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only
"""Write canonical ZIP metadata/order without relying on a particular zlib version."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import zipfile


def normalize(source, destination, epoch):
    if not 315532800 <= epoch <= 4354819198:
        raise ValueError('SOURCE_DATE_EPOCH must be within the ZIP timestamp range')
    date = datetime.fromtimestamp(epoch, timezone.utc).timetuple()[:6]
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_STORED) as result:
        names = original.namelist()
        if len(names) != len(set(names)):
            raise ValueError('Duplicate ZIP entries are not reproducible inputs')
        for name in sorted(names):
            if name.startswith('/') or '..' in Path(name).parts:
                raise ValueError('Unsafe ZIP entry')
            info = zipfile.ZipInfo(name, date)
            info.create_system = 3
            info.external_attr = ((0o40755 if name.endswith('/') else 0o100644) << 16)
            info.compress_type = zipfile.ZIP_STORED
            result.writestr(info, original.read(name))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source')
    parser.add_argument('destination')
    parser.add_argument('--epoch', type=int, required=True)
    args = parser.parse_args()
    normalize(args.source, args.destination, args.epoch)
