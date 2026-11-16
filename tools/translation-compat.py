#!/usr/bin/env python3
# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only

"""Inspect translation patches or convert shared resource formats for OpenBGI."""
import argparse
import json
from pathlib import Path
import sys
import subprocess

from translation_compat.pipeline import (
    aplib_subprocess, convert, inspect_game, plan_report, read_profile,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    inspect = sub.add_parser('inspect', help='Read-only format and DLL evidence inventory')
    inspect.add_argument('game', type=Path)
    inspect.add_argument('--depth', type=int, choices=range(5), default=2)
    inspect.add_argument('--report', type=Path)
    for name in ('plan', 'convert'):
        command = sub.add_parser(name)
        command.add_argument('game', type=Path)
        command.add_argument('--profile', type=Path, required=True)
        if name == 'convert':
            command.add_argument('--out', type=Path, required=True)
            command.add_argument('--aplib-python', type=Path,
                                 help='Explicit optional decoder Python with aplib installed')
        else:
            command.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'inspect':
            result = inspect_game(args.game, args.depth)
        elif args.command == 'plan':
            result = plan_report(args.game, read_profile(args.profile))
        else:
            decoder = aplib_subprocess(args.aplib_python) if args.aplib_python else None
            result = convert(args.game, read_profile(args.profile), args.out, decoder)
        serialized = json.dumps(result, ensure_ascii=True, indent=2) + '\n'
        report = getattr(args, 'report', None)
        if report:
            with report.open('x', encoding='utf-8') as stream:
                stream.write(serialized)
            print(f'Wrote {report}')
        elif args.command == 'convert':
            print(f'Converted {len(result["resources"])} resources to {args.out}')
        else:
            print(serialized, end='')
    except (ValueError, OSError, TimeoutError, subprocess.SubprocessError) as error:
        print(f'translation-compat: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
