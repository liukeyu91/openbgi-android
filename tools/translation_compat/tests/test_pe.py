# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only

"""Self-created PE fixtures; no game code or resources are used."""
import struct
import sys
sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[2]))
import tempfile
import unittest
from pathlib import Path
from translation_compat.pe_probe import inspect_pe, PEFormatError


def toy_pe(bits=32):
    data = bytearray(0x800)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    optional_size = 224 if bits == 32 else 240
    struct.pack_into('<HHIIIHH', data, 0x84, 0x14c if bits == 32 else 0x8664, 1, 123, 0, 0, optional_size, 0x2102)
    optional = 0x98
    struct.pack_into('<H', data, optional, 0x10b if bits == 32 else 0x20b)
    struct.pack_into('<I', data, optional + 16, 0x1000)
    struct.pack_into('<I' if bits == 32 else '<Q', data, optional + (28 if bits == 32 else 24), 0x400000)
    struct.pack_into('<I', data, optional + 60, 0x200)
    struct.pack_into('<I', data, optional + (92 if bits == 32 else 108), 16)
    directory = optional + (96 if bits == 32 else 112)
    struct.pack_into('<II', data, directory, 0x1200, 0x100)
    struct.pack_into('<II', data, directory + 8, 0x1100, 40)
    struct.pack_into('<8sIIIIIIHHI', data, optional + optional_size, b'.test\0\0\0', 0x600, 0x1000, 0x600, 0x200, 0, 0, 0, 0, 0x40000040)
    struct.pack_into('<IIIII', data, 0x300, 0x1140, 0, 0, 0x1170, 0x1140)
    fmt = '<III' if bits == 32 else '<QQQ'
    struct.pack_into(fmt, data, 0x340, 0x1180, (1 << (bits - 1)) | 7, 0)
    data[0x370:0x37d] = b'KERNEL32.dll\0'
    struct.pack_into('<H', data, 0x380, 9)
    data[0x382:0x392] = b'LoadLibraryA\0xxx'
    struct.pack_into('<IIHHIIIIIII', data, 0x400, 0, 0, 0, 0, 0x1260, 1, 2, 1, 0x1240, 0x1248, 0x124c)
    struct.pack_into('<II', data, 0x440, 0x1000, 0x1270)
    struct.pack_into('<I', data, 0x448, 0x1280)
    struct.pack_into('<H', data, 0x44c, 0)
    data[0x460:0x468] = b'toy.dll\0'
    data[0x470:0x47f] = b'OTHER.Function\0'
    data[0x480:0x486] = b'Probe\0'
    data[0x500:0x50a] = b'patch.cpk\0'
    return data


class PEProbeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'toy.dll'

    def parse(self, data):
        self.path.write_bytes(data)
        return inspect_pe(self.path)

    def test_pe32_and_pe32plus_imports_exports(self):
        for bits in (32, 64):
            with self.subTest(bits=bits):
                result = self.parse(toy_pe(bits))
                self.assertEqual(result['bits'], bits)
                self.assertEqual(result['imports'][0]['module'], 'KERNEL32.dll')
                self.assertEqual(result['imports'][0]['symbols'], [{'name': 'LoadLibraryA', 'hint': 9}, {'ordinal': 7}])
                self.assertEqual(result['exports'][0]['names'], ['Probe'])
                self.assertEqual(result['exports'][1]['forwarder'], 'OTHER.Function')
                self.assertTrue(any(c['value'] == 'patch.cpk' for c in result['string_candidates']))
                self.assertEqual(result['evidence_kind'], 'offline_pe_static_parse')

    def test_truncated_headers_and_sections(self):
        for length in (0, 63, 0x83, 0x97, 0x100, 0x1ff, 0x7ff):
            with self.subTest(length=length):
                with self.assertRaises(PEFormatError): self.parse(toy_pe()[:length])

    def test_bad_rva_and_unterminated_import(self):
        data = toy_pe()
        struct.pack_into('<I', data, 0x300, 0x9999)
        with self.assertRaises(PEFormatError): self.parse(data)
        data = toy_pe()
        struct.pack_into('<IIIII', data, 0x314, 0x1140, 0, 0, 0x1170, 0x1140)
        with self.assertRaisesRegex(PEFormatError, 'Unterminated'): self.parse(data)

    def test_bad_export_ordinal_and_count(self):
        data = toy_pe()
        struct.pack_into('<H', data, 0x44c, 3)
        with self.assertRaisesRegex(PEFormatError, 'ordinal'): self.parse(data)
        data = toy_pe()
        struct.pack_into('<I', data, 0x414, 1000000)
        with self.assertRaisesRegex(PEFormatError, 'count'): self.parse(data)

    def test_ambiguous_sections_rejected(self):
        data = toy_pe()
        struct.pack_into('<H', data, 0x86, 2)
        struct.pack_into('<8sIIIIIIHHI', data, 0x98 + 224 + 40, b'.other\0\0', 16, 0x1000, 16, 0x700, 0, 0, 0, 0, 0)
        with self.assertRaisesRegex(PEFormatError, 'overlapping'): self.parse(data)


if __name__ == '__main__':
    unittest.main()
