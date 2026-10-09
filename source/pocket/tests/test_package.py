"""Static package/ABI guards; no Pocket execution is implied."""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from package import CORE_JSON_FILES, verify_elf

class PackageTests(unittest.TestCase):
    def test_elf_gate(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "test.elf"
            head = bytearray(52)
            head[:7] = b"\x7fELF\x01\x01\x01"
            struct.pack_into("<H", head, 18, 243)
            struct.pack_into("<I", head, 36, 3)
            path.write_bytes(head)
            verify_elf(path)
            for machine, flags in ((40, 3), (243, 5), (243, 1)):
                struct.pack_into("<H", head, 18, machine)
                struct.pack_into("<I", head, 36, flags)
                path.write_bytes(head)
                with self.assertRaises(ValueError): verify_elf(path)

    def test_slots_match(self):
        data = json.loads((ROOT / "config/data.json").read_text())["data"]["data_slots"]
        instances = json.loads((ROOT / "config/instance.json").read_text())["instance"]["data_slots"]
        slots = {item["id"]: item for item in data}
        self.assertEqual(len(slots), len(data))
        for item in instances:
            self.assertIn(item["id"], slots)
            self.assertLessEqual(len(item["filename"]), 23)
            self.assertIn(item["filename"].split(".")[-1], slots[item["id"]]["extensions"])
        self.assertEqual(next(item["filename"] for item in instances if item["id"] == 4), "pal.pak")
        for slot in range(10, 15):
            self.assertTrue(slots[slot]["nonvolatile"])

    def test_core_metadata(self):
        core = json.loads((ROOT / "config/core.json").read_text())["core"]
        self.assertEqual(core["cores"][0]["filename"], "os25.rbf_r")
        self.assertFalse(core["framework"]["sleep_supported"])
        self.assertEqual(core["metadata"]["platform_ids"], ["sdlpal"])

    def test_core_json_set_includes_variants(self):
        # The Pocket rejects a core folder without variants.json ("load error in variants").
        self.assertIn("variants.json", CORE_JSON_FILES)
        for name in CORE_JSON_FILES:
            json.loads((ROOT / "config" / name).read_text())
        variants = json.loads((ROOT / "config/variants.json").read_text())["variants"]
        self.assertEqual(variants["magic"], "APF_VER_1")
        self.assertEqual(variants["variant_list"], [])

if __name__ == "__main__": unittest.main()
