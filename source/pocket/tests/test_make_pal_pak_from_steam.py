# SPDX-License-Identifier: GPL-3.0-only
"""Steam -> pal.pak helper tests. All fixture files are synthetic fakes."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import make_pal_pak_from_steam as steam  # noqa: E402
from pack_assets import read_archive  # noqa: E402


class SteamPakTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.pal = base / "steamapps/common/PAL"
        dos = self.pal / "PAL_DOS"
        dos.mkdir(parents=True)
        for name in steam.DATA_FILES:
            (dos / name.upper()).write_bytes(b"fake " + name.encode())
        (dos / "PAL.EXE").write_bytes(b"MZ")      # must not be packed
        (dos / "1.RPG").write_bytes(b"save")      # must not be packed
        (dos / "MUS.MKF").write_bytes(b"midi")    # must not be packed
        (self.pal / "PAL98").mkdir()
        self.workshop = base / "steamapps/workshop/content/1546570"
        self.out = base / "out"

    def music(self, workshop_id, numbers, width=2, sub="ogg"):
        folder = self.workshop / workshop_id / sub
        folder.mkdir(parents=True)
        for number in numbers:
            (folder / f"{number:0{width}d}.OGG").write_bytes(b"OggS fake %d" % number)

    def test_arranged_default_location(self):
        self.music("2449282849", range(1, 88))
        result = steam.build(self.pal, "arranged", self.out)
        names = {entry["name"] for entry in read_archive(self.out / "pal.pak")}
        self.assertEqual(result["tracks"], 87)
        self.assertEqual(names, set(steam.DATA_FILES) | {f"ogg/{n:02d}.ogg" for n in range(1, 88)})

    def test_sc_three_digit_names_without_29(self):
        self.music("2433259482", [n for n in range(1, 88) if n != 29], width=3, sub="")
        result = steam.build(self.pal, "sc", self.out)
        self.assertEqual(result["tracks"], 86)
        self.assertTrue((self.out / "stage/ogg/30.ogg").is_file())
        self.assertFalse((self.out / "stage/ogg/29.ogg").exists())

    def test_missing_track_and_data_rejected(self):
        self.music("2449282849", range(1, 87))
        with self.assertRaisesRegex(ValueError, "missing \\[87\\]"):
            steam.build(self.pal, "arranged", self.out)
        (self.pal / "PAL_DOS/WOR16.FON").unlink()
        with self.assertRaisesRegex(ValueError, "wor16.fon"):
            steam.build(self.pal, "arranged", self.base_out("b"))

    def test_refuses_existing_output_and_non_ogg(self):
        self.music("2449282849", range(1, 88))
        (self.out).mkdir()
        (self.out / "pal.pak").write_bytes(b"keep")
        with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
            steam.build(self.pal, "arranged", self.out)
        self.assertEqual((self.out / "pal.pak").read_bytes(), b"keep")
        (self.workshop / "2449282849/ogg/05.OGG").write_bytes(b"RIFF")
        with self.assertRaisesRegex(ValueError, "Not an Ogg"):
            steam.build(self.pal, "arranged", self.base_out("c"))

    def base_out(self, name):
        return Path(self.temp.name) / name


if __name__ == "__main__":
    unittest.main()
