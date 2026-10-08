# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1] / "fpga/verify_boot_image.py"
spec = importlib.util.spec_from_file_location("verify_boot_image", MODULE)
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)

class BootImageTests(unittest.TestCase):
    def test_actual_words_and_failure_cases(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            mif, boot = base / "firmware.mif", base / "boot.bin"
            boot.write_bytes(bytes.fromhex("78563412efcdab89"))
            header = "WIDTH=32;\nDEPTH=4;\nADDRESS_RADIX=DEC;\nDATA_RADIX=HEX;\nCONTENT BEGIN\n"
            valid = header + "0 : 12345678;\n1 : 89ABCDEF;\n[2..3] : 00000013;\nEND;\n"
            mif.write_text(valid)
            self.assertEqual(validator.verify_boot_image(mif, boot)["boot_words"], 2)
            for invalid in [header + "[0..3] : 00000013;\nEND;\n",
                            valid.replace("12345678", "00000000"),
                            valid.replace("[2..3]", "[1..3]"),
                            valid.replace("[2..3]", "[2..2]"),
                            valid.replace("[2..3]", "[2..4]")]:
                mif.write_text(invalid)
                with self.assertRaises(ValueError): validator.verify_boot_image(mif, boot)
