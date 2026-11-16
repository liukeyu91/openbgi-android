#!/usr/bin/env python3
# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only
"""Check an explicitly supplied release key before packaging; never create one."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def preflight(root, manifest, key, alias):
    root, key = Path(root).resolve(), Path(key).resolve()
    if root == key or root in key.parents:
        raise ValueError('Release signing keys must be outside the checkout and its build directories')
    if not key.is_file():
        raise ValueError('Release signing key is missing; refusing to generate or replace it')
    if not os.environ.get('OPENBGI_STORE_PASS'):
        raise ValueError('Set OPENBGI_STORE_PASS for the persistent release key')
    package = ET.parse(manifest).getroot().attrib['package']
    pins = json.loads((root / 'tools/android/release-certificates.json').read_text(encoding='utf-8'))
    expected = os.environ.get('OPENBGI_EXPECTED_CERT_SHA256') or pins.get(package)
    if not expected or len(expected) != 64 or any(c not in '0123456789abcdefABCDEF' for c in expected):
        raise ValueError('Unknown release identity; explicitly configure its certificate SHA-256')
    result = subprocess.run(['keytool', '-exportcert', '-keystore', str(key), '-storetype', 'PKCS12',
        '-storepass:env', 'OPENBGI_STORE_PASS', '-alias', alias], capture_output=True)
    if result.returncode:
        raise ValueError('Cannot read the release certificate; check the key, alias and password')
    actual = hashlib.sha256(result.stdout).hexdigest()
    if actual != expected.lower():
        raise ValueError('Signing certificate differs from the pinned release identity; refusing an incompatible update')
    return actual


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--keystore', required=True)
    parser.add_argument('--alias', required=True)
    args = parser.parse_args()
    try:
        print(preflight(args.root, args.manifest, args.keystore, args.alias))
    except (ValueError, OSError, ET.ParseError) as error:
        parser.exit(1, str(error) + '\n')
