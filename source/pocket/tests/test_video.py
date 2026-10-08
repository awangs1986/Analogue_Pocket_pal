# SPDX-License-Identifier: GPL-3.0-only
"""Asset-free contract tests for the real Pocket video backend and its JSON."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
POCKET = ROOT / "pocket"
SDK = Path(os.environ.get("SDK_ROOT", ROOT.parent / "openfpgaSDK")).resolve()


class VideoTests(unittest.TestCase):
    def test_actual_backend_with_padded_surfaces_and_rotating_pages(self):
        self.assertTrue((SDK / "src/sdk/include/SDL.h").exists(),
                        "Set SDK_ROOT to the pinned openfpgaSDK checkout")
        with tempfile.TemporaryDirectory(prefix="pal-video-") as directory:
            exe = Path(directory) / "video-test"
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c99", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=undefined", "-fno-sanitize-recover=all",
                "-I" + str(POCKET / "tests/video_stubs"), "-I" + str(POCKET),
                "-I" + str(ROOT / "sdlpal"), "-I" + str(ROOT / "sdlpal/sdl_compat"),
                "-I" + str(SDK / "src/sdk/include"), str(POCKET / "video.c"),
                str(POCKET / "tests/video_backend_test.c"), "-o", str(exe),
            ]
            subprocess.run(command, check=True)
            subprocess.run([str(exe)], check=True)

    def test_scaler_slot_and_aspect_contract(self):
        video = json.loads((POCKET / "config/video.json").read_text())["video"]
        self.assertEqual(video["magic"], "APF_VER_1")
        # Slot numbers are a core/runtime ABI. Never move 320x200 to slot 0.
        self.assertEqual([(m["width"], m["height"]) for m in video["scaler_modes"]],
                         [(320, 240), (320, 200), (320, 224), (320, 256),
                          (320, 288), (400, 300), (256, 240), (640, 480)])
        mode = video["scaler_modes"][1]
        self.assertEqual((mode["aspect_w"], mode["aspect_h"]), (4, 3))
        self.assertEqual((mode["dock_aspect_w"], mode["dock_aspect_h"]), (4, 3))
        self.assertEqual((mode["rotation"], mode["mirror"]), (0, 0))

    def test_only_aspect_preserving_non_grayscale_display_mode_is_advertised(self):
        video = json.loads((POCKET / "config/video.json").read_text())["video"]
        # LCDs may force square-pixel integer scaling. Grayscale also needs RTL
        # conversion and 0x444D response; the pinned core implements neither.
        self.assertEqual(video["display_modes"], [{"id": "0x10"}])


if __name__ == "__main__":
    unittest.main()
