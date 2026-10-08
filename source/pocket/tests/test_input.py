# SPDX-License-Identifier: GPL-3.0-only
"""Asset-free tests of the actual input backend against the SDK input ABI."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
POCKET = ROOT / "pocket"
SDK = Path(os.environ.get("SDK_ROOT", ROOT.parent / "openfpgaSDK")).resolve()


class InputTests(unittest.TestCase):
    def test_actual_backend_mappings_and_state_machine(self):
        self.assertTrue((SDK / "src/sdk/include/of_input_types.h").exists(),
                        "Set SDK_ROOT to the pinned openfpgaSDK checkout")
        with tempfile.TemporaryDirectory(prefix="pal-input-") as directory:
            exe = Path(directory) / "input-test"
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c99", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=undefined", "-fno-sanitize-recover=all",
                "-I" + str(POCKET / "tests/input_stubs"), "-I" + str(POCKET),
                "-I" + str(ROOT / "sdlpal"), "-I" + str(ROOT / "sdlpal/sdl_compat"),
                "-I" + str(SDK / "src/sdk/include"), str(POCKET / "input.c"),
                str(POCKET / "tests/input_backend_test.c"), "-o", str(exe),
            ]
            subprocess.run(command, check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
