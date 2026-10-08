# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual terminal-dialog adapter with asset-free SDK service doubles."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
POCKET = ROOT / "pocket"
SDK = Path(os.environ.get("SDK_ROOT", ROOT.parent / "openfpgaSDK")).resolve()


class PlatformTests(unittest.TestCase):
    def test_actual_dialog_palette_input_and_geometry_lifecycle(self):
        self.assertTrue((SDK / "src/sdk/include/SDL.h").exists(),
                        "Set SDK_ROOT to the pinned openfpgaSDK checkout")
        with tempfile.TemporaryDirectory(prefix="pal-platform-") as directory:
            exe = Path(directory) / "platform-test"
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c99", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-fsanitize=undefined", "-fno-sanitize-recover=all",
                "-I" + str(POCKET / "tests/platform_stubs"), "-I" + str(POCKET),
                "-I" + str(ROOT / "sdlpal"), "-I" + str(ROOT / "sdlpal/sdl_compat"),
                "-I" + str(SDK / "src/sdk/include"), str(POCKET / "platform.c"),
                str(POCKET / "tests/platform_backend_test.c"), "-o", str(exe),
            ]
            subprocess.run(command, check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True,
                                    check=True, timeout=10)
            self.assertEqual(result.stdout.count("\x1b[0m\x1b[2J\x1b[H"), 5)
            self.assertIn("platform dialog contracts passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
