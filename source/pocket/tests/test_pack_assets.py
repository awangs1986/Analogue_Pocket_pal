# SPDX-License-Identifier: GPL-3.0-only
"""Desktop conformance tests. All fixture bytes are newly synthesized here."""
from pathlib import Path
import importlib.util
import json
import os
import struct
import subprocess
import tempfile
import unittest

POCKET = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("pack_assets", POCKET / "tools/pack_assets.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.source = self.base / "assets"
        self.source.mkdir()
        (self.source / "DaTa.MKF").write_bytes(b"0123456789")
        (self.source / "OGG").mkdir()
        (self.source / "OGG/001.OGG").write_bytes(b"OGG-xtest")
        (self.source / "empty.dat").write_bytes(b"")
        (self.source / "sdlpal.cfg").write_bytes(b"Music=OGG\n")
        self.output = self.base / "pal.pak"

    def build(self):
        return pack.pack_archive(self.source, self.output)

    def test_deterministic_roundtrip_and_excluded_saves(self):
        (self.source / "1.RPG").write_bytes(b"private-save")
        (self.source / "PAL_1.sav").write_bytes(b"private-save")
        records = self.build()
        before = self.output.read_bytes()
        for path in self.source.rglob("*"):
            if path.is_file():
                os.utime(path, (1234567890, 1234567890))
        self.assertEqual(records, self.build())
        self.assertEqual(before, self.output.read_bytes())
        self.assertEqual(records, pack.read_archive(self.output))
        self.assertEqual([r["name"] for r in records],
                         ["data.mkf", "empty.dat", "ogg/001.ogg", "sdlpal.cfg"])
        for record in records:
            self.assertEqual(record["offset"] % 16, 0)
        self.assertNotIn(b"private-save", before)

    def test_canonical_collision_rejected(self):
        (self.source / "data.mkf").write_bytes(b"collision")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.build()

    def test_symlink_rejected(self):
        (self.source / "outside").symlink_to(self.base)
        with self.assertRaisesRegex(ValueError, "links"):
            self.build()

    def test_unsafe_names_rejected(self):
        for name in ("", "/x", "../x", "a/../b", "a//b", "./x", "C:x", "a\\b", "中文", "x" * 120, "a\x00b"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                pack.canonical_name(name)

    def test_output_inside_source_not_packed(self):
        self.output = self.source / "pal.pak"
        first = self.build()
        self.assertEqual(first, self.build())
        self.assertEqual(len(first), 4)

    def test_empty_or_oversized_sources_rejected(self):
        empty = self.base / "empty"
        empty.mkdir()
        (empty / "1.rpg").write_bytes(b"save")
        with self.assertRaisesRegex(ValueError, "empty"):
            pack.pack_archive(empty, self.output)
        huge = self.source / "huge.dat"
        with huge.open("wb") as f:
            f.truncate(pack.MAX_SIZE + 1)
        with self.assertRaisesRegex(ValueError, "limit"):
            self.build()

    def test_total_limit_and_directory_limit_rejected(self):
        # Sparse files exercise arithmetic without reading/allocating gigabytes.
        with (self.source / "huge.dat").open("wb") as f:
            f.truncate(pack.MAX_SIZE - 128)
        with self.assertRaisesRegex(ValueError, "seek limit"):
            self.build()
        (self.source / "huge.dat").unlink()
        original = pack.MAX_FILES
        try:
            pack.MAX_FILES = 3
            with self.assertRaisesRegex(ValueError, "at most"):
                self.build()
        finally:
            pack.MAX_FILES = original

    def compile_native(self):
        binary = self.base / "files_test"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-DPAL_POCKET_FILES_HOST", str(POCKET / "files.c"),
                   str(POCKET / "tests/files_test.c"), "-o", str(binary)]
        subprocess.run(command, check=True)
        return binary

    def test_corrupt_archives_rejected(self):
        binary = self.compile_native()
        self.build()
        original = self.output.read_bytes()
        mutations = {
            "short header": original[:16],
            "truncated data": original[:-1],
            "trailing bytes": original + b"x",
            "bad magic": b"BADPAK!!" + original[8:],
        }
        def patch(offset, data):
            result = bytearray(original)
            result[offset:offset + len(data)] = data
            return result
        mutations.update({
            "version": patch(8, struct.pack("<I", 2)),
            "zero count": patch(12, bytes(4)),
            "huge count": patch(12, struct.pack("<I", 0xFFFFFFFF)),
            "wrong table": patch(20, struct.pack("<I", 128)),
            "reserved": patch(28, struct.pack("<I", 1)),
            "unterminated name": patch(32, b"x" * 120),
            "traversal": patch(32, b"../x\0" + bytes(115)),
            "uppercase": patch(32, b"DATA.MKF"),
            "nonzero padding": patch(32 + 20, b"x"),
            "table overlap": patch(32 + 120, struct.pack("<I", 32)),
            "offset overflow": patch(32 + 120, struct.pack("<I", 0xFFFFFFF0)),
            "size overflow": patch(32 + 124, struct.pack("<I", 0xFFFFFFFF)),
            "duplicate name": patch(32 + 128, original[32:32 + 120]),
        })
        for label, content in mutations.items():
            with self.subTest(label=label):
                bad = self.base / "bad.pak"
                bad.write_bytes(content)
                with self.assertRaises(ValueError):
                    pack.read_archive(bad)
                result = subprocess.run([str(binary), str(bad), "reject"], cwd=self.base,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_sdk_slot_contract(self):
        document = json.loads((POCKET / "config/data.json").read_text())
        slots = {s["id"]: s for s in document["data"]["data_slots"]}
        self.assertEqual(slots[4]["extensions"], ["pak"])
        self.assertTrue(slots[4]["deferload"])
        for slot in (8, 10, 11, 12, 13, 14):
            self.assertTrue(slots[slot]["nonvolatile"])
            self.assertEqual(int(slots[slot]["size_maximum"], 16), 256 * 1024)
            self.assertEqual(int(slots[slot]["parameters"], 16), 0x84)

    def test_linker_interposition(self):
        self.build()
        binary = self.base / "files_wrap_test"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-DPAL_POCKET_FILES_HOST", str(POCKET / "files.c"),
                        str(POCKET / "tests/files_wrap_test.c"),
                        "-Wl,--wrap=fopen,--wrap=access", "-o", str(binary)], check=True)
        result = subprocess.run([str(binary), str(self.output)], cwd=self.base,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.base / "PAL_4.sav").read_bytes(), b"link-test")

    def test_native_stdio_and_saves(self):
        self.build()
        binary = self.compile_native()
        result = subprocess.run([str(binary), str(self.output)], cwd=self.base,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("failure reporting passed", result.stdout)
        self.assertEqual((self.base / "PAL_1.sav").read_bytes(), b"save-v2!")


if __name__ == "__main__":
    unittest.main()
