# SPDX-License-Identifier: GPL-3.0-only
"""PAL98 text converter and pal98 packer edition. Synthetic fixtures only."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import pal98_text  # noqa: E402
import make_pal_pak_from_steam as steam  # noqa: E402
from pack_assets import read_archive  # noqa: E402


def fake_mkf(chunks):
    count = len(chunks)
    offsets, pos = [], 4 * (count + 1)
    for c in chunks:
        offsets.append(pos)
        pos += len(c)
    offsets.append(pos)
    return struct.pack("<%dI" % (count + 1), *offsets) + b"".join(chunks)


def scripts(entries):
    return b"".join(struct.pack("<HHHH", *e) for e in entries)


def utf16(lines):
    return "\r\n".join(lines).encode("utf-16")   # with BOM, like Steam


# Script: [0] 0x0001; [1..3] dialog 10,11 + restore + 12; [5] dialog 20 then
# dialog 25 (not consecutive); [8] dialog 30 reached only by a jump (op 0x0002 -> 8).
ENTRIES = [(0x0001, 0, 0, 0), (0xFFFF, 10, 0, 0), (0xFFFF, 11, 0, 0), (0x008E, 0, 0, 0),
           (0xFFFF, 12, 0, 0), (0x0000, 0, 0, 0), (0xFFFF, 20, 0, 0), (0xFFFF, 25, 0, 0),
           (0xFFFF, 26, 0, 0), (0x0002, 8, 0, 0), (0x0000, 0, 0, 0)]
MESSAGES = {10: "$10李～逍～遙！~30", 11: "李逍遙:", 12: "  要殺要剮'不用'多說！", 20: "-青色-",
            25: '"鐵銹"', 26: "@", 30: "unused"}


class Pal98TextTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        lines = [";", "; header comment", ""] + [f"{k}:{v}" for k, v in MESSAGES.items()]
        (self.dir / "MSG_cht.txt").write_bytes(utf16(lines))
        (self.dir / "WORD_cht.txt").write_bytes(utf16([";", "0:", "1:", "2:經驗值", "3:風雪冰天1"]))
        (self.dir / "Sss.mkf").write_bytes(fake_mkf([b"\0\0", b"\0\0", b"\0\0", b"", scripts(ENTRIES)]))

    def convert(self, variant="cht"):
        out = self.dir / "out.msg"
        report = pal98_text.convert(self.dir / "MSG_cht.txt", self.dir / "WORD_cht.txt",
                                    self.dir / "Sss.mkf", out, variant)
        return report, out.read_text(encoding="utf-8").splitlines()

    def block(self, lines, sid, eid):
        start = lines.index(f"[BEGIN MESSAGE] {sid}")
        end = lines.index(f"[END MESSAGE] {eid}", start)
        return lines[start + 1:end]

    def test_runs_follow_message_get_span(self):
        report, lines = self.convert()
        self.assertEqual(report["conflicts"], 0)
        self.assertEqual(report["missing_ids"], [])
        # Consecutive ids with an interleaved 0x008E become one block with a clear.
        self.assertEqual(self.block(lines, 10, 12),
                         ["$10李～逍～遙！~30", "李逍遙:", "[CLEAR MESSAGE]", "  要殺要剮'不用'多說！"])
        # Non-consecutive next id ends the block at once; 25/26 is its own run.
        self.assertEqual(self.block(lines, 20, 20), ["-青色-"])
        self.assertEqual(self.block(lines, 25, 26), ['"鐵鏽"', "@"])   # glyph substitution
        # Jump target into the middle of a run gets its own block too.
        self.assertIn("[BEGIN MESSAGE] 26", lines)
        self.assertNotIn("unused", "\n".join(lines))

    def test_words_and_settings(self):
        _, lines = self.convert()
        self.assertIn("FontFlavor=TradChin", lines)
        words = lines[lines.index("[BEGIN WORDS]") + 1:lines.index("[END WORDS]")]
        self.assertEqual(words, ["1=", "2=經驗值", "3=風雪冰天"])   # DOS-style '1' suffix strip
        _, lines = self.convert("chs")
        self.assertIn("FontFlavor=SimpChin", lines)

    def test_bad_line_rejected(self):
        (self.dir / "MSG_cht.txt").write_bytes(utf16(["10 no colon"]))
        with self.assertRaisesRegex(ValueError, "unrecognized line"):
            self.convert()


class Pal98PakTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.pal = base / "steamapps/common/PAL"
        d = self.pal / "PAL98"
        (d / "ogg").mkdir(parents=True)
        (self.pal / "PAL_DOS").mkdir()
        for name in steam.PAL98_DATA_FILES:
            data = fake_mkf([b"\0\0", b"\0\0", b"\0\0", b"", scripts(ENTRIES)]) if name == "sss.mkf" else b"fake"
            (d / name.capitalize()).write_bytes(data)
        for n in range(1, 7):
            (d / f"{n}.AVI").write_bytes(b"RIFF\0\0\0\0AVI LIST")
        (d / "MSG_cht.txt").write_bytes(utf16([f"{k}:{v}" for k, v in MESSAGES.items()]))
        (d / "WORD_cht.txt").write_bytes(utf16(["2:經驗值"]))
        for n in steam.PAL98_CD_TRACKS:
            (d / "ogg" / f"{10000 + n}.ogg").write_bytes(b"OggS cd")
        (d / "PAL.exe").write_bytes(b"MZ")      # must not be packed
        (d / "SDL2.dll").write_bytes(b"MZ")     # must not be packed
        self.music = base / "music"
        self.music.mkdir()
        for n in range(1, 88):
            (self.music / f"{n:02d}.ogg").write_bytes(b"OggS %d" % n)
        self.out = base / "out"

    def test_pal98_archive_contents(self):
        result = steam.build(self.pal, "arranged", self.out, self.music, edition="pal98")
        names = {e["name"] for e in read_archive(self.out / "pal.pak")}
        expected = (set(steam.PAL98_DATA_FILES) | set(steam.PAL98_MOVIES) |
                    {f"ogg/{n:02d}.ogg" for n in range(1, 88)} |
                    {f"ogg/{10000 + n}.ogg" for n in steam.PAL98_CD_TRACKS} |
                    {"pal98_cht.msg", "sdlpal.cfg"})
        self.assertEqual(names, expected)
        self.assertEqual(result["cd_tracks"], 12)
        self.assertEqual((self.out / "stage/sdlpal.cfg").read_text(), "MessageFileName=pal98_cht.msg\n")

    def test_pal98_without_cd_tracks_and_checks(self):
        steam.build(self.pal, "arranged", self.out, self.music, edition="pal98", cd_tracks=False)
        names = {e["name"] for e in read_archive(self.out / "pal.pak")}
        self.assertFalse(any(n.startswith("ogg/100") for n in names))
        (self.pal / "PAL98/6.AVI").write_bytes(b"not an avi")
        with self.assertRaisesRegex(ValueError, "Not an AVI"):
            steam.build(self.pal, "arranged", self.out.parent / "o2", self.music, edition="pal98")
        (self.pal / "PAL98/MSG_chs.txt").unlink(missing_ok=True)
        with self.assertRaisesRegex(ValueError, "msg_chs.txt"):
            steam.build(self.pal, "arranged", self.out.parent / "o3", self.music,
                        edition="pal98", text_variant="chs")
        with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
            steam.build(self.pal, "arranged", self.out, self.music, edition="pal98")


if __name__ == "__main__":
    unittest.main()
