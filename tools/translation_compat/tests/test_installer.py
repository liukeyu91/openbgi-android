# SPDX-License-Identifier: GPL-2.0-only

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

from translation_compat.formats import FormatError
from translation_compat.installer import convert_installer, select_files


class InstallerTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.source = self.root / "patch.exe"
        self.source.write_bytes(b"fixture")
        self.output = self.root / "patch.zip"

    @staticmethod
    def extract_fixture(source, work, format_name, unpacker):
        extracted = work / "inno"
        archive = extracted / "app" / "Archive"
        archive.mkdir(parents=True)
        (archive / "日本語.dat").write_bytes(b"payload")
        (extracted / "app" / "BGI.gdb").write_bytes(b"progress")
        (extracted / "app" / "hook.dll").write_bytes(b"patch loader")
        (extracted / "tmp").mkdir()
        (extracted / "tmp" / "README").write_bytes(b"temporary")
        return "inno", extracted

    def test_installation_root_and_metadata(self):
        manifest = self.root / "metadata.json"
        manifest.write_text('{"encoding":"big5","target":{"engine":"1.680"}}', encoding="utf-8")
        with patch("translation_compat.installer.extract", self.extract_fixture):
            result = convert_installer(self.source, self.output, manifest=manifest)
            self.assertEqual(result, ("inno", 1))
            with ZipFile(self.output) as archive:
                self.assertEqual(archive.namelist(), ["archive/日本語.dat", ".openbgi/patch.json"])
                self.assertEqual(archive.read("archive/日本語.dat"), b"payload")
                data = json.loads(archive.read(".openbgi/patch.json"))
            self.assertEqual(data["encoding"], "big5")
            self.assertEqual(data["target"], {"engine": "1.680"})
            original = self.output.read_bytes()
            with self.assertRaises(FormatError):
                convert_installer(self.source, self.output)
            self.assertEqual(self.output.read_bytes(), original)

    def test_without_manifest(self):
        with patch("translation_compat.installer.extract", self.extract_fixture):
            convert_installer(self.source, self.output)
        with ZipFile(self.output) as archive:
            self.assertEqual(archive.namelist(), ["archive/日本語.dat"])

    def test_delta_requires_complete_resource(self):
        (self.root / "resource.xdelta").write_bytes(b"delta")
        (self.root / "system.arc").write_bytes(b"replacement")
        with self.assertRaisesRegex(FormatError, "resource.xdelta"):
            select_files(self.root, (), ())
        result = select_files(self.root, ("*.ARC",), ())
        self.assertEqual(list(result), ["system.arc"])

    def test_missing_input_root_leaves_no_zip(self):
        with patch("translation_compat.installer.extract", self.extract_fixture):
            with self.assertRaises(FormatError):
                convert_installer(self.source, self.output, root="../outside")
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob(".openbgi-installer-*")), [])


if __name__ == "__main__":
    unittest.main()
