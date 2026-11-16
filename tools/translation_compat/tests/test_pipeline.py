# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only

"""Synthetic archives and profiles; no commercial files are test fixtures."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import subprocess

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from translation_compat.formats import BgiArc, EvbExternal, EvbPe, FormatError
from translation_compat.pipeline import convert, inside, inspect_game, plan_report, read_profile


def arc(entries, arc20=True):
    name_size, record_size = (96, 128) if arc20 else (16, 32)
    index, payload = bytearray(), bytearray()
    for name, data in entries:
        record = bytearray(record_size)
        encoded = name.encode('cp932')
        record[:len(encoded)] = encoded
        struct.pack_into('<II', record, name_size, len(payload), len(data))
        index += record
        payload += data
    return ((b'BURIKO ARC20' if arc20 else b'PackFile    ')
            + struct.pack('<I', len(entries)) + index + payload)


def evb(entries, compressed=False):
    root = struct.pack('<I8xI', 0, len(entries)) + '%DEFAULT FOLDER%'.encode('utf-16-le') + b'\0\0\x03' + bytes(25)
    nodes, payload = bytearray(root), bytearray()
    for name, data in entries:
        stored = struct.pack('<III', 12, 0, len(data)) + data if compressed else data
        meta = bytearray(53)
        struct.pack_into('<I', meta, 2, len(data))
        struct.pack_into('<I', meta, 49, len(stored))
        nodes += struct.pack('<I8xI', 0, 0) + name.encode('utf-16-le') + b'\0\0\x02' + meta
        payload += stored
    header = bytearray(79)
    header[:4] = b'EVB\0'
    struct.pack_into('<I', header, 64, 79 + len(nodes) + 4 - 68)
    return header + nodes + bytes(4) + payload


def pe_carrier(payload):
    data = bytearray(512)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 64)
    data[64:68] = b'PE\0\0'
    # A loader's literal signature is not an archive and must be ignored.
    data[100:104] = b'EVB\0'
    return data + payload


class PipelineTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.base = Path(temp.name)
        self.game = self.base / 'game'
        self.game.mkdir()
        self.profile = {'schema': 1, 'id': 'synthetic', 'runtime': {'text_encoding': 'gbk'},
                        'sources': [{'format': 'bgi-arc', 'path': 'text.arc', 'select': ['*']}]}

    def write_arc(self, entries=None):
        path = self.game / 'text.arc'
        path.write_bytes(arc(entries or [('intro', '中文'.encode('gbk'))]))
        return path

    def test_bgi_variants_preserve_stored_streams(self):
        for new in (False, True):
            with self.subTest(arc20=new):
                path = self.game / 'renamed.bin'
                data = b'DSC FORMAT 1.00\0synthetic-compressed-stream'
                path.write_bytes(arc([('intro', data)], new))
                archive = BgiArc(path)
                self.assertEqual(archive.read(archive.entries[0]), data)

    def test_bgi_index_errors_and_case_collisions(self):
        path = self.write_arc([('one', b'a'), ('ONE', b'b')])
        with self.assertRaises(FormatError): BgiArc(path)
        for bad in ('../intro', 'con.txt', 'intro.', 'evil:name'):
            path.write_bytes(arc([(bad, b'a')]))
            with self.assertRaises(FormatError): BgiArc(path)
        data = bytearray(arc([('intro', b'a')]))
        struct.pack_into('<I', data, 16 + 96, 0xffffffff)
        path.write_bytes(data)
        with self.assertRaises(FormatError): BgiArc(path)
        path.write_bytes(data[:20])
        with self.assertRaises(FormatError): BgiArc(path)

    def test_evb_stored_and_compressed(self):
        path = self.game / 'arbitrary.extension'
        for compressed in (False, True):
            path.write_bytes(evb([('intro', b'toy')], compressed))
            archive = EvbExternal(path, decoder=lambda data, maximum: data)
            self.assertEqual(archive.read(archive.entries[0]), b'toy')

    def test_evb_requires_explicit_decoder(self):
        path = self.game / 'toy.klt'
        path.write_bytes(evb([('intro', b'toy')], True))
        archive = EvbExternal(path)
        with self.assertRaisesRegex(FormatError, 'decoder'): archive.read(archive.entries[0])

    def test_evb_rejects_malformed_table_and_payload(self):
        path = self.game / 'toy.klt'
        good = evb([('intro', b'toy')], True)
        for bad in (good[:60], good[:80], good[:-1]):
            path.write_bytes(bad)
            with self.assertRaises(FormatError): EvbExternal(path)
        path.write_bytes(good)
        archive = EvbExternal(path, decoder=lambda data, maximum: b'wrong-length')
        with self.assertRaises(FormatError): archive.read(archive.entries[0])
        bad = bytearray(good)
        offset = archive.entries[0].offset
        struct.pack_into('<I', bad, offset + 8, 99999)
        path.write_bytes(bad)
        with self.assertRaises(FormatError): archive.read(archive.entries[0])

    def test_evb_does_not_flatten_nested_directories(self):
        path = self.game / 'toy.klt'
        data = evb([('intro', b'toy')])
        data = data.replace('%DEFAULT FOLDER%'.encode('utf-16-le'), 'Other Directory!'.encode('utf-16-le'))
        path.write_bytes(data)
        with self.assertRaises(FormatError): EvbExternal(path)

    def test_evb_rejects_directory_count_smaller_than_table(self):
        path = self.game / 'bad.klt'
        data = bytearray(evb([('one', b'a'), ('two', b'b')]))
        struct.pack_into('<I', data, 79 + 12, 1)
        path.write_bytes(data)
        with self.assertRaisesRegex(FormatError, 'complete table'): EvbExternal(path)

    def test_embedded_evb_reuses_reader_with_validated_base(self):
        path = self.game / 'renamed.exe'
        path.write_bytes(pe_carrier(evb([('intro', b'toy')])))
        archive = EvbPe(path)
        self.assertEqual(archive.base_offset, 512)
        self.assertEqual(archive.read(archive.entries[0]), b'toy')
        self.assertEqual(archive.entries[0].offset, 512 + EvbExternal(
            self.write_external_evb()).entries[0].offset)

    def write_external_evb(self):
        path = self.game / 'external.klt'
        path.write_bytes(evb([('intro', b'toy')]))
        return path

    def test_embedded_evb_ambiguity_requires_explicit_base(self):
        path = self.game / 'ambiguous.exe'
        first = evb([('one', b'a')])
        path.write_bytes(pe_carrier(first + evb([('two', b'b')])))
        with self.assertRaisesRegex(FormatError, 'found 2'): EvbPe(path)
        archive = EvbPe(path, base_offset=512 + len(first))
        self.assertEqual(archive.entries[0].name, 'two')
        self.assertEqual(archive.read(archive.entries[0]), b'b')

    def test_embedded_evb_does_not_accept_magic_only(self):
        path = self.game / 'fake.exe'
        for data in (pe_carrier(b'EVB\0' + bytes(128)), b'MZ' + bytes(1024)):
            path.write_bytes(data)
            with self.assertRaises(FormatError): EvbPe(path)

    def test_profile_validation_and_no_commands(self):
        path = self.base / 'profile.json'
        for mutation in ({'schema': 2}, {'command': 'anything'},
                         {'schema': True},
                         {'runtime': {'text_encoding': 'auto'}},
                         {'runtime': {'engine': '1.6; command'}},
                         {'runtime': {'font_family': '\n'}}, {'sources': []}):
            value = {**self.profile, **mutation}
            path.write_text(json.dumps(value))
            with self.assertRaises(FormatError): read_profile(path)
        path.write_text(json.dumps(self.profile))
        self.assertEqual(read_profile(path), self.profile)
        for wrong in ([], {}, 4, None):
            self.profile['sources'][0]['format'] = wrong
            path.write_text(json.dumps(self.profile))
            with self.assertRaises(FormatError): read_profile(path)

    def test_excludes_progress_and_windows_binaries(self):
        self.write_arc([('intro', b'toy'), ('BGI.gdb', b'progress'),
                        ('BGI.chs', b'progress'), ('patch.dll', b'MZ'), ('slot.sav', b'progress'),
                        ('FriendToLoverHD000.sud', b'progress'), ('BGI0001.cad', b'progress')])
        out = self.base / 'overlay'
        report = convert(self.game, self.profile, out)
        self.assertEqual([r['name'] for r in report['resources']], ['intro'])
        self.assertEqual(len(report['skipped']), 6)
        self.assertEqual((out / 'intro').read_bytes(), b'toy')
        self.assertTrue((out / 'openbgi-translation-manifest.json').exists())

    def test_input_paths_cannot_escape_or_use_progress_directory(self):
        self.write_arc()
        (self.base / 'outside.arc').write_bytes(arc([('intro', b'x')]))
        for name in ('../outside.arc', str(self.base / 'outside.arc'), 'UserData/slot.arc'):
            self.profile['sources'][0]['path'] = name
            with self.assertRaises((FormatError, FileNotFoundError)): plan_report(self.game, self.profile)

    def test_output_cannot_overwrite_or_modify_original(self):
        path = self.write_arc()
        before = path.read_bytes()
        for out in (self.game / 'overlay', self.game, self.base):
            with self.assertRaises(FormatError): convert(self.game, self.profile, out)
        self.assertEqual(path.read_bytes(), before)

    def test_progress_aliases_are_excluded_after_resolution(self):
        progress = self.game / 'UserData'
        progress.mkdir()
        (progress / 'slot.arc').write_bytes(arc([('intro', b'toy')]))
        alias = self.game / 'alias'
        try:
            alias.symlink_to(progress, target_is_directory=True)
        except OSError:
            if sys.platform != 'win32': self.skipTest('Host does not support symlinks')
            run = subprocess.run(['cmd', '/c', 'mklink', '/J', str(alias), str(progress)],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if run.returncode: self.skipTest('Host does not support junctions')
        with self.assertRaises(FormatError): inside(self.game.resolve(), 'alias/slot.arc')
        self.assertEqual(inspect_game(self.game)['files'], [])

    def test_concurrent_output_creation_is_not_replaced(self):
        self.write_arc()
        destination = self.base / 'overlay'
        def competing_output(entry):
            destination.mkdir()
            (destination / 'sentinel').write_bytes(b'keep')
            return b'toy'
        with patch.object(BgiArc, 'read', side_effect=competing_output):
            with self.assertRaises(FileExistsError): convert(self.game, self.profile, destination)
        self.assertEqual((destination / 'sentinel').read_bytes(), b'keep')

    def test_case_insensitive_cross_source_collision(self):
        self.write_arc([('intro', b'x')])
        (self.game / 'other.arc').write_bytes(arc([('INTRO', b'y')]))
        self.profile['sources'].append({'format': 'bgi-arc', 'path': 'other.arc', 'select': ['*']})
        with self.assertRaises(FormatError): plan_report(self.game, self.profile)

    def test_failed_conversion_leaves_no_partial_overlay(self):
        self.write_arc([('one', b'a'), ('two', b'b')])
        out = self.base / 'overlay'
        with patch.object(BgiArc, 'read', side_effect=[b'a', FormatError('damaged payload')]):
            with self.assertRaises(FormatError): convert(self.game, self.profile, out)
        self.assertFalse(out.exists())
        self.assertFalse(list(self.base.glob('.translation-*')))

    def test_plan_does_not_decode_payload(self):
        self.write_arc()
        with patch.object(BgiArc, 'read', side_effect=AssertionError('must not decode')):
            report = plan_report(self.game, self.profile)
        self.assertEqual(report['resources'][0]['name'], 'intro')

    def test_inspection_uses_magic_and_skips_progress(self):
        (self.game / 'unknown.file').write_bytes(evb([('intro', b'x')]))
        (self.game / 'BGI.gdb').write_bytes(b'private')
        report = inspect_game(self.game)
        self.assertEqual(len(report['files']), 1)
        self.assertEqual(report['files'][0]['format'], 'evb-external')


if __name__ == '__main__':
    unittest.main()
