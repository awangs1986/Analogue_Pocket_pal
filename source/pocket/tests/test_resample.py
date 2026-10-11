# SPDX-License-Identifier: GPL-3.0-only
"""Host build of the fixed-point SFX/AVI resampler and the SFX cache."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

POCKET = Path(__file__).resolve().parents[1]


class ResampleTests(unittest.TestCase):
    def test_fir_cache_and_streaming(self):
        with tempfile.TemporaryDirectory(prefix="pal-resample-") as directory:
            exe = Path(directory) / "resample-test"
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c99", "-D_GNU_SOURCE", "-O1", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-I" + str(POCKET), str(POCKET / "pocket_resample.c"), str(POCKET / "sfx_cache.c"),
                str(POCKET / "tests/resample_test.c"), str(POCKET / "tests/pocket_test_stubs.c"), "-lm", "-o", str(exe)]
            subprocess.run(command, check=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
