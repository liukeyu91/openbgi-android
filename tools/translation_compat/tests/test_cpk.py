# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only

"""Synthetic CPK converter checks. No game files or patch binaries are used.

Run from project root: python -m unittest discover -s tools/translation_compat/tests -v
"""
import struct
import sys
sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[2]))
import tempfile
import unittest
import zlib
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, modes
from cryptography.hazmat.primitives.cmac import CMAC
from translation_compat.formats import CpkV2
from translation_compat.pipeline import convert
try:
    from cryptography.hazmat.decrepit.ciphers.algorithms import Camellia
except ImportError:
    from cryptography.hazmat.primitives.ciphers.algorithms import Camellia

class Archive(CpkV2):
    # Adapt the original independent regression vectors to the shared reader API.
    def decode(self, name):
        return self.read(next(e for e in self.entries if e.name == name))
    def public_index(self):
        return [e.public() for e in self.entries]
    @property
    def records(self):
        return {e.name: {'packed':e.stored} for e in self.entries}



def xor_repeat(data, key):
    return bytes(value ^ key[index % len(key)] for index, value in enumerate(data))


def encrypt_payload(plaintext, key, nonce):
    """Build test ciphertext independently of Archive.decode's chunk loop."""
    mac = CMAC(Camellia(key))
    mac.update(bytes(16) + nonce)
    counter = int.from_bytes(mac.finalize(), 'big')
    encryptor = Cipher(Camellia(key), modes.ECB()).encryptor()
    encrypted = bytearray()
    for offset in range(0, len(plaintext), 16):
        stream = encryptor.update(counter.to_bytes(16, 'big'))
        block = plaintext[offset:offset + 16]
        encrypted.extend(a ^ b for a, b in zip(block, stream))
        counter += 1
    encryptor.finalize()
    return bytes(encrypted)


def fixture(entries, *, key_count=None, cpk_count=None):
    """Encode toy (name, bytes, optional overrides) entries into CPK/key bytes."""
    count = len(entries)
    header_key = bytes(range(32))
    key_header = struct.pack('<4sI', b'CPK\0', count if key_count is None else key_count) + header_key
    header = bytearray(140)
    header[:8] = b'CPK\0\x02\0\0\0'
    struct.pack_into('<I', header, 136, count if cpk_count is None else cpk_count)
    records, index, payload = [], [], bytearray()
    for number, (name, data, overrides) in enumerate(entries):
        index_key = bytes((i + number) % 256 for i in range(32))
        key = bytes((i + 41 + number) % 256 for i in range(16))
        nonce = bytes((i + 91 + number) % 256 for i in range(16))
        packed_plain = overrides.get('packed_plain', zlib.compress(data))
        encrypted = encrypt_payload(packed_plain, key, nonce)
        entry = name.encode('ascii').ljust(32, b'\0')
        if len(entry) != 32:
            raise ValueError('Test entry name too long')
        entry += struct.pack('<3I', overrides.get('unpacked', len(data)),
                             overrides.get('packed', len(encrypted)),
                             overrides.get('offset', len(payload)))
        key_record = index_key + struct.pack('<2I', overrides.get('keylen', 16),
                                            overrides.get('ivlen', 16)) + key + nonce
        index.append(xor_repeat(entry, index_key))
        records.append(xor_repeat(key_record, b'\x13'))
        payload.extend(encrypted)
    return (xor_repeat(header, header_key) + b''.join(index) + payload,
            xor_repeat(key_header, b'\x13') + b''.join(records))


class ConverterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.cpk = Path(self.temp.name) / 'toy.cpk'
        self.key = Path(self.temp.name) / 'toy.key'

    def write(self, entries=None, **kwargs):
        cpk, key = fixture(entries or [('intro', b'Synthetic UTF-8: \xe4\xb8\xad\xe6\x96\x87', {})], **kwargs)
        self.cpk.write_bytes(cpk)
        self.key.write_bytes(key)
        return cpk, key

    def archive(self, **kwargs):
        return Archive(self.cpk, self.key, **kwargs)

    def test_round_trip_multiple_sizes_and_chunk_boundaries(self):
        # Deterministic incompressible data exercises >64 KiB ciphertext chunks.
        import random
        rng = random.Random(1993)
        large = rng.randbytes(150123)
        entries = [('empty', b'', {}), ('intro', '中文小样'.encode(), {}),
                   ('large', large, {}), ('repeat', b'abc' * 100000, {})]
        self.write(entries)
        archive = self.archive()
        self.assertGreater(archive.records['large']['packed'], 2 * 65536)
        for name, data, _ in entries:
            self.assertEqual(archive.decode(name), data)
        self.assertEqual([row['name'] for row in archive.public_index()], [row[0] for row in entries])

    def test_wrong_header_key(self):
        _, key = self.write()
        changed = bytearray(key)
        changed[8] ^= 1
        self.key.write_bytes(changed)
        with self.assertRaises(ValueError):
            self.archive()

    def test_wrong_payload_key(self):
        _, key = self.write()
        changed = bytearray(key)
        changed[40 + 40] ^= 1
        self.key.write_bytes(changed)
        with self.assertRaises((ValueError, zlib.error)):
            self.archive().decode('intro')

    def test_bad_magic_and_key_count(self):
        for mutation in ('magic', 'count', 'truncated', 'extra'):
            with self.subTest(mutation=mutation):
                _, key = self.write()
                changed = bytearray(key)
                if mutation == 'magic': changed[0] ^= 1
                if mutation == 'count': changed[4] ^= 1
                if mutation == 'truncated': changed = changed[:39]
                if mutation == 'extra': changed += b'x'
                self.key.write_bytes(changed)
                with self.assertRaises(ValueError): self.archive()

    def test_cpk_count_mismatch(self):
        self.write(cpk_count=2)
        with self.assertRaises(ValueError): self.archive()

    def test_path_escape_and_duplicate_names(self):
        for name in ('../intro', '..\\intro', '/intro', 'C:intro', '.', '..', ''):
            with self.subTest(name=name):
                self.write([(name, b'x', {})])
                with self.assertRaises(ValueError): self.archive()
        self.write([('intro', b'a', {}), ('intro', b'b', {})])
        with self.assertRaises(ValueError): self.archive()

    def test_invalid_sizes_and_payload_bounds(self):
        for overrides in ({'offset': 0xffffffff}, {'packed': 0xffffffff},
                          {'unpacked': 0xffffffff}, {'keylen': 32}, {'ivlen': 8}):
            with self.subTest(overrides=overrides):
                self.write([('intro', b'x', overrides)])
                with self.assertRaises(ValueError): self.archive()

    def test_overlapping_payloads(self):
        self.write([('intro', b'a', {}), ('other', b'b', {'offset': 0})])
        with self.assertRaises(ValueError): self.archive()

    def test_truncated_index_and_payload(self):
        for length in (139, 140 + 43):
            with self.subTest(length=length):
                cpk, _ = self.write()
                self.cpk.write_bytes(cpk[:length])
                with self.assertRaises(ValueError): self.archive()
        cpk, _ = self.write()
        self.cpk.write_bytes(cpk[:-1])
        with self.assertRaises(ValueError): self.archive()
        cpk, _ = self.write()
        archive = self.archive()
        self.cpk.write_bytes(cpk[:-1])
        with self.assertRaises(ValueError): archive.decode('intro')

    def test_declared_decoded_length_mismatch(self):
        for size in (2, 100):
            with self.subTest(size=size):
                self.write([('intro', b'synthetic', {'unpacked': size})])
                with self.assertRaises(ValueError): self.archive().decode('intro')

    def test_zlib_truncation_and_trailing_data(self):
        for stream in (zlib.compress(b'toy')[:-1], zlib.compress(b'toy') + b'garbage'):
            with self.subTest(stream=stream):
                self.write([('intro', b'toy', {'packed_plain': stream})])
                with self.assertRaises(ValueError): self.archive().decode('intro')

    def test_expanded_cpk_graphics_preserved_as_native_images(self):
        image = struct.pack('<HHIII', 2, 2, 32, 0, 0) + bytes(16)
        self.write([('picture', image, {})])
        profile = {'schema': 1, 'id': 'pixels', 'sources': [
            {'format': 'cpk-v2', 'path': self.cpk.name, 'key': self.key.name, 'select': ['*']}]}
        with tempfile.TemporaryDirectory() as output:
            destination = Path(output) / 'overlay'
            result = convert(self.cpk.parent, profile, destination)
            self.assertEqual((destination / 'picture').read_bytes(), image)
            self.assertEqual(result['resources'][0]['content_kind'], 'bgi-raw-image')


if __name__ == '__main__':
    unittest.main()
