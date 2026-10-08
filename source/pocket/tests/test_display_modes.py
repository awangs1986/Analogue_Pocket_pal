# SPDX-License-Identifier: GPL-3.0-only
"""Source-only APF Display Modes tests; never changes the SDK or ships a bitstream."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
POCKET = ROOT / "pocket"
FPGA = POCKET / "fpga"
CORE = Path(os.environ.get("CORE_ROOT", ROOT.parent / "openfpgaCore")).resolve()
REV = "618a3eb985759a4154115109c2c8036271252888"
BASE = "src/fpga/targets/pocket/"
PATCH = FPGA / "display-modes.patch"
SOURCE_PATHS = [BASE + p for p in ["core_bridge_cmd.v", "core_top.v", "ap_core.qsf",
                                   "apf/common.v"]]
SOURCE_PATHS += ["src/fpga/test/tb_core_bridge_cmd.v"]


def snapshot_and_patch(directory):
    root = Path(directory)
    for name in SOURCE_PATHS:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(subprocess.check_output(["git", "-C", str(CORE), "show", REV + ":" + name]))
    subprocess.run(["git", "-C", str(root), "apply", "--check", str(PATCH)], check=True)
    subprocess.run(["git", "-C", str(root), "apply", str(PATCH)], check=True)
    return root


class DisplayModesTests(unittest.TestCase):
    def test_patch_applies_to_exact_runtime_without_changing_source(self):
        before = subprocess.check_output(["git", "-C", str(CORE), "status", "--porcelain=v1"])
        with tempfile.TemporaryDirectory(prefix="pal-display-source-") as directory:
            root = snapshot_and_patch(directory)
            subprocess.run(["git", "-C", str(root), "apply", "--reverse", "--check", str(PATCH)], check=True)
            qsf = (root / BASE / "ap_core.qsf").read_text()
            self.assertIn("set_global_assignment -name VERILOG_FILE pal_display_mode_video.v", qsf)
            top = (root / BASE / "core_top.v").read_text()
            # New stage is after the pre-existing blanking mux and all APF
            # outputs go through it. The Analogizer path remains unchanged.
            self.assertIn(".rgb_in(video_rgb_core)", top)
            for name in ["rgb", "de", "skip", "vs", "hs"]:
                self.assertIn(f".{name}_out(video_{name})", top)
            self.assertIn(".scanout_reset_n(reset_n_vid)", top)
            self.assertIn("use_analog_dedicated ? analogizer_scan_rgb : vidout_rgb;", top)
            for name in SOURCE_PATHS:
                path = root / name
                self.assertTrue(path.is_file())
            subprocess.run(["git", "-C", str(root), "apply", "--reverse", str(PATCH)], check=True)
            for name in SOURCE_PATHS:
                expected = subprocess.check_output(["git", "-C", str(CORE), "show", REV + ":" + name])
                self.assertEqual((root / name).read_bytes(), expected)
        after = subprocess.check_output(["git", "-C", str(CORE), "status", "--porcelain=v1"])
        self.assertEqual(after, before)

    def simulator_commands(self):
        compiler = shlex.split(os.environ.get("IVERILOG", "iverilog"))
        runner = shlex.split(os.environ.get("VVP", "vvp"))
        available = all(command and shutil.which(command[0]) for command in [compiler, runner])
        if not available:
            if os.environ.get("PAL_REQUIRE_RTL") == "1":
                self.fail("Icarus Verilog and vvp required; set IVERILOG and VVP")
            self.skipTest("Install Icarus Verilog or set IVERILOG/VVP; simulation NOT verified")
        return compiler, runner

    def simulate(self, root, show_warnings=True, parameters=()):
        compiler, runner = self.simulator_commands()
        exe = root / "display_modes.vvp"
        overrides = [f"-Ptb_display_modes.{name}={value}" for name, value in parameters]
        compile_result = subprocess.run(compiler + overrides + [
            "-g2012", "-Wall", "-s", "tb_display_modes", "-o", str(exe),
            str(root / BASE / "core_bridge_cmd.v"),
            str(root / BASE / "pal_display_mode_video.v"),
            str(root / BASE / "apf/common.v"),
            str(POCKET / "tests/display_modes/mf_datatable_model.v"),
            str(POCKET / "tests/display_modes/tb_display_modes.v")], text=True, capture_output=True)
        self.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)
        # The upstream RTL has no timescale; testbench delays all have one.
        # Keep compiler warnings visible, but a compile failure never counts
        # as a killed mutation or as a behavioral pass.
        if show_warnings and compile_result.stderr:
            print(compile_result.stderr, end="")
        return subprocess.run(runner + [str(exe)], text=True, capture_output=True, timeout=15)

    def test_rtl_simulation(self):
        self.simulator_commands()
        with tempfile.TemporaryDirectory(prefix="pal-display-sim-") as directory:
            result = self.simulate(snapshot_and_patch(directory))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end="")
            self.assertIn("PASS display modes", result.stdout)
            self.assertIn("PASS 00B8 both endians", result.stdout)

    def test_clock_phase_matrix(self):
        self.simulator_commands()
        variants = [(7, 19, 0), (7, 21, 0), (6, 20, 0), (8, 20, 0),
                    (11, 17, 0), (7, 20, 1), (6, 23, 1), (9, 19, 1)]
        with tempfile.TemporaryDirectory(prefix="pal-display-clocks-") as directory:
            root = snapshot_and_patch(directory)
            for bridge_half, video_half, initial_level in variants:
                with self.subTest(bridge_half=bridge_half, video_half=video_half,
                                  initial_level=initial_level):
                    result = self.simulate(root, show_warnings=False, parameters=[
                        ("BRIDGE_HALF_PERIOD", bridge_half),
                        ("VIDEO_HALF_PERIOD", video_half),
                        ("VIDEO_INITIAL_LEVEL", initial_level)])
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("PASS display modes", result.stdout)
                    print(f"PASS clock/phase variant: {bridge_half}/{video_half}/{initial_level}")

    def test_behavioral_mutations_are_detected(self):
        self.simulator_commands()
        mutations = [
            ("missing grayscale", "pal_display_mode_video.v",
             "(grayscale_next && de_in)", "(1'b0)", "Pixel/control mismatch"),
            ("corrupted APF blanking", "pal_display_mode_video.v",
             "(grayscale_next && de_in)", "(grayscale_next)", "Pixel/control mismatch"),
            ("broken full-range luma", "pal_display_mode_video.v",
             "(red << 2) + red", "(red << 2)", "Pixel/control mismatch"),
            ("misaligned sideband", "pal_display_mode_video.v",
             "de_out <= de_in;", "de_out <= ~de_in;", "sidebands not aligned"),
            ("premature success", "core_bridge_cmd.v",
             "if (display_request_applied) begin\r\n            osnotify_display_mode", "if (1'b1) begin\r\n            osnotify_display_mode",
             "Reset-time ACK before grayscale application"),
            ("ignored grayscale request bit", "core_bridge_cmd.v",
             "osnotify_grayscale <= host_20[0];",
             "osnotify_grayscale <= (host_20[15:8] == 8'h20);", "Wrong response data"),
            ("mid-frame mode change", "pal_display_mode_video.v",
             "(!scanout_reset_n || (vs_in && !de_in))", "(1'b1)",
             "Display mode changed inside a frame"),
            ("new request accepted during cancellation", "core_bridge_cmd.v",
             "if (display_recovery != 0)", "if (1'b0)", "Bad status"),
            ("removed timeout", "core_bridge_cmd.v",
             "display_mode_wait_cycles == DISPLAY_MODE_TIMEOUT_CYCLES - 1'b1",
             "1'b0", "failed to terminate"),
        ]
        for title, name, original, replacement, reason in mutations:
            with self.subTest(mutation=title), tempfile.TemporaryDirectory(prefix="pal-display-mutant-") as directory:
                root = snapshot_and_patch(directory)
                path = root / BASE / name
                source = path.read_bytes()
                self.assertEqual(source.count(original.encode()), 1)
                path.write_bytes(source.replace(original.encode(), replacement.encode()))
                result = self.simulate(root, show_warnings=False)
                self.assertNotEqual(result.returncode, 0, f"Surviving mutation: {title}")
                self.assertIn(reason, result.stdout + result.stderr)
                print(f"PASS mutation detected: {title}")

    def test_optional_profiles_preserve_scaler_abi_and_default_remains_crt_only(self):
        current = json.loads((POCKET / "config/video.json").read_text())["video"]
        self.assertEqual(current["display_modes"], [{"id": "0x10"}])
        catalog = json.loads((FPGA / "profiles/catalog.json").read_text())
        advertised = set()
        v1_profiles = []
        for profile in catalog["profiles"]:
            data = json.loads((FPGA / "profiles" / profile["file"]).read_text())["video"]
            self.assertEqual(data["scaler_modes"], current["scaler_modes"])
            ids = [int(mode["id"], 16) for mode in data["display_modes"]]
            self.assertGreater(len(ids), 1)
            self.assertLessEqual(len(ids), 16)
            self.assertEqual(len(ids), len(set(ids)))
            self.assertNotIn(0, ids)
            contains_monochrome = bool(set(ids) & {0x20, 0x21, 0x22, 0x23})
            self.assertEqual(profile["requires_new_verified_runtime"], contains_monochrome)
            if not profile["requires_new_verified_runtime"]:
                v1_profiles.append(profile["file"])
                self.assertEqual(profile["compatible_runtime"], "existing-pinned-v1-os25")
                self.assertEqual(profile["readiness"], "CONFIG_READY_HARDWARE_UNVERIFIED")
                self.assertEqual(ids, [0x10,0x30,0x31,0x32,0x40,0x41,0x42,0x51,
                                       0x52,0x61,0x62,0x63,0x71,0x72,0x81,0x82])
            self.assertTrue(profile["lcd_aspect_not_guaranteed"])
            self.assertEqual(profile["expected_mode_ids"], [f"0x{i:02X}" for i in ids])
            advertised.update(ids)
        self.assertEqual(v1_profiles, ["color-v1.video.json"])
        self.assertEqual(advertised, {0x10,0x20,0x21,0x22,0x23,0x30,0x31,0x32,
                                      0x40,0x41,0x42,0x51,0x52,0x61,0x62,0x63,
                                      0x71,0x72,0x81,0x82,0xE0,0xE1})
        provenance = json.loads((FPGA / "source.json").read_text())
        self.assertEqual(provenance["runtime_source_commit"], REV)
        self.assertEqual(provenance["patch_sha256"], hashlib.sha256(PATCH.read_bytes()).hexdigest())
        self.assertEqual(provenance["status"], "SOURCE_ONLY_NOT_A_RUNTIME")


if __name__ == "__main__":
    unittest.main()
