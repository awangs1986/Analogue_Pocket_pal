#!/usr/bin/env python3
"""Portable orchestration tests. No Docker, compiler, Quartus or network needed."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("build_handoff", ROOT / "pocket/tools/build_handoff.py")
handoff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(handoff)


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *map(str, args)], stderr=subprocess.DEVNULL, text=True).strip()


def repo(path, files):
    path.mkdir(parents=True)
    git(path, "init", "-q")
    git(path, "config", "user.name", "Fixture")
    git(path, "config", "user.email", "fixture@example.invalid")
    for name, data in files.items():
        output = path / name
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(data)
    git(path, "add", ".")
    git(path, "commit", "-qm", "fixture")
    return git(path, "rev-parse", "HEAD")


class FakeRunner:
    """Execute only tiny local Git/Python fixtures; fake all container commands."""
    def __init__(self):
        self.commands = []
        self.device = "HANDOFF_DEVICE=5CEBA4F23C8"
        self.image_os = "linux"
        self.image_arch = "amd64"
        self.os_release = 'ID=ubuntu\nVERSION_ID="22.04"'
        self.fail_if = None
        self.hook = None

    def run(self, args, cwd=None):
        args = [str(a) for a in args]
        self.commands.append(args)
        if self.fail_if and self.fail_if(args):
            raise handoff.BuildError("Injected command failure")
        if self.hook:
            response = self.hook(args)
            if response is not None:
                return response
        if args[0] in ("git", sys.executable):
            result = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if result.returncode:
                raise handoff.BuildError(result.stdout)
            return result.stdout.strip()
        if args[:2] == ["docker", "info"]:
            return json.dumps({"OSType": "linux"})
        if args[:3] == ["docker", "context", "inspect"]:
            return "unix:///var/run/docker.sock"
        if args[:3] == ["docker", "image", "inspect"]:
            return json.dumps([{"Id": "sha256:" + "a" * 64, "Os": self.image_os, "Architecture": self.image_arch}])
        if args[:2] == ["docker", "run"]:
            if args[-2:] == ["cat", "/etc/os-release"]:
                return self.os_release
            if args[-2:] == ["quartus_sh", "--version"]:
                return "Quartus Prime Version 25.1std.0 Build 1129 Lite Edition"
            if "-t" in args and "quartus_sh" in args:
                return self.device
            return ""
        raise AssertionError("Unexpected command: " + repr(args))


@unittest.skipUnless(shutil.which("git"), "local Git needed for fixture repositories")
class BuildHandoffTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="pal-handoff-test-")
        self.base = Path(self.tmp.name)
        self.root, self.sdk, self.core = [self.base / s for s in ("source", "sdk", "core")]
        self.vexii, self.spinal, self.rvls = [self.base / s for s in ("vexii", "spinal", "rvls")]
        self.work = self.base / "out"
        self.lock = json.loads((ROOT / "pocket/build-lock.json").read_text())
        spinal = repo(self.spinal, {"build.sbt": "// fixture\n"})
        rvls = repo(self.rvls, {"README": "rvls fixture\n"})
        repo(self.vexii, {"build.sbt": "// fixture\n"})
        for name, commit in (("SpinalHDL", spinal), ("rvls", rvls)):
            git(self.vexii, "update-index", "--add", "--cacheinfo", f"160000,{commit},ext/{name}")
        git(self.vexii, "commit", "-qm", "nested pinned gitlinks")
        vexii = git(self.vexii, "rev-parse", "HEAD")
        repo(self.core, {"README": "core fixture\n", "src/fpga/targets/pocket/ap_core.qsf": "set_global_assignment -name DEVICE 5CEBA4F23C8\n"})
        git(self.core, "update-index", "--add", "--cacheinfo", f"160000,{vexii},{handoff.VEXII}")
        git(self.core, "commit", "-qm", "vexii pinned gitlink")
        core = git(self.core, "rev-parse", "HEAD")
        sdk_files = {"src/sdk/musl/lib/" + n: "fixture" for n in ("libc.a", "libm.a", "crt1.o", "crti.o", "crtn.o")}
        manifest = "# source: 618a3eb\n"
        for name in ("os25.rbf_r", "os.bin", "loader.bin"):
            sdk_files["runtime/pocket/" + name] = "OLD_CANDIDATE_" + name
            manifest += hashlib.md5(sdk_files["runtime/pocket/" + name].encode()).hexdigest() + "  ./pocket/" + name + "\n"
        sdk_files["runtime/MANIFEST"] = manifest
        sdk = repo(self.sdk, sdk_files)
        baseline = repo(self.root, {"sdlpal/main.c": "int main(void) { return 0; }\n", "pocket/Makefile": "all:\n\t@true\n", "pocket/fpga/display-modes.patch": "fixture patch\n"})
        self.lock["baseline_commit"] = baseline
        self.lock["sdk"]["commit"] = sdk
        self.lock["core"]["commit"] = core
        for sub, commit in zip(self.lock["submodules"], (vexii, spinal, rvls)):
            sub["commit"] = commit
        self.lock["display_patch"]["sha256"] = handoff.sha256(self.root / "pocket/fpga/display-modes.patch")
        (self.root / "pocket/build-lock.json").write_text(json.dumps(self.lock))
        helper = '''import argparse, pathlib, subprocess, json
p=argparse.ArgumentParser(); p.add_argument('--source-core'); p.add_argument('--output'); a=p.parse_args()
subprocess.run(['git','clone','--no-local',a.source_core,a.output],check=True)
out=pathlib.Path(a.output)
(out/'src/fpga/targets/pocket/pal_display_mode_video.v').write_text('module pal_display_mode_video; endmodule\\n')
(out/'PAL_DISPLAY_MODES_SOURCE_ONLY.json').write_text(json.dumps({'patch_sha256': PATCH_HASH}))
'''.replace("PATCH_HASH", repr(self.lock["display_patch"]["sha256"]))
        (self.root / "pocket/fpga/prepare_runtime.py").write_text(helper)
        self.runner = FakeRunner()
        self.args = handoff.parser().parse_args(["plan", "--sdk", str(self.sdk), "--core", str(self.core), "--work", str(self.work), "--vexii", str(self.vexii), "--spinal", str(self.spinal), "--rvls", str(self.rvls)])
        self.h = handoff.Handoff(self.args, self.root, self.runner)

    def tearDown(self):
        self.tmp.cleanup()

    def available(self):
        return mock.patch.object(handoff.shutil, "which", return_value="/fixture/bin/tool")

    def test_plan_is_read_only_and_relocatable(self):
        plan = self.h.plan()
        self.assertEqual([], self.runner.commands)
        self.assertFalse(self.work.exists())
        self.assertIn(str(self.base), json.dumps(plan))
        self.assertNotIn("/workspace/shared", json.dumps(plan))
        self.assertFalse(plan["release_ready"])

    def test_lock_has_exact_requested_pins_no_absolute_paths(self):
        lock = json.loads((ROOT / "pocket/build-lock.json").read_text())
        self.assertEqual("618a3eb985759a4154115109c2c8036271252888", lock["core"]["commit"])
        self.assertEqual("a408ddc12aed0dfaa4aa22c06af82f829db77126", lock["sdk"]["commit"])
        self.assertEqual("580b76c3868512c8316bb7a3d3add81cad49a0dc", lock["submodules"][0]["commit"])
        self.assertNotIn("/workspace/", json.dumps(lock))
        self.assertFalse(lock["policy"]["runtime_publication"])

    def test_pin_mismatch_stops_before_build(self):
        self.h.lock["sdk"]["commit"] = "0" * 40
        with self.available(), self.assertRaisesRegex(handoff.BuildError, "SDK pin mismatch"):
            self.h.preflight("game")
        self.assertFalse(any(a[:2] == ["docker", "run"] for a in self.runner.commands))

    def test_missing_submodule_never_falls_back_or_fetches(self):
        self.h.sources["spinal"] = self.base / "uninitialized"
        with self.assertRaisesRegex(handoff.BuildError, "uninitialized"):
            self.h.check_sources()
        self.assertFalse(any("update" in a or "fetch" in a or "clone" in a for a in self.runner.commands))

    def test_wrong_gitlink_fails_even_when_checkout_pin_matches(self):
        self.h.lock["submodules"][1]["gitlink"] = "ext/NotSpinal"
        with self.assertRaisesRegex(handoff.BuildError, "gitlink"):
            self.h.check_sources()

    def test_prepare_uses_only_local_exact_sources(self):
        with self.available():
            result = self.h.execute("prepare-core")
        self.assertEqual("passed", result["status"])
        self.assertEqual(self.lock["core"]["commit"], git(self.work / "core", "rev-parse", "HEAD"))
        for sub in self.lock["submodules"]:
            self.assertEqual(sub["commit"], git(self.work / "core" / sub["path"], "rev-parse", "HEAD"))
        self.h.prepared()
        self.assertFalse((self.work / "core/runtime/MANIFEST").exists())
        self.assertFalse(any(a[0] == "docker" for a in self.runner.commands))
        self.assertFalse(any("--recursive" in a or "fetch" in a for a in self.runner.commands))

    def test_failed_stage_records_failure_and_short_circuits(self):
        self.runner.fail_if = lambda a: "image" in a
        with self.available(), self.assertRaises(handoff.BuildError):
            self.h.execute("game")
        state = json.loads(self.h.state_path.read_text())
        self.assertEqual("failed", state["stages"]["game"]["status"])
        self.assertFalse(self.h.pal.exists())
        self.assertFalse(any("make" in a for a in self.runner.commands))

    def test_sdk_mixed_runtime_rejected_even_with_rewritten_manifest(self):
        output = self.sdk / "runtime/pocket/os.bin"
        output.write_text("NEW_UNRELATED_OS")
        manifest = self.sdk / "runtime/MANIFEST"
        old = hashlib.md5(b"OLD_CANDIDATE_os.bin").hexdigest()
        manifest.write_text(manifest.read_text().replace(old, hashlib.md5(output.read_bytes()).hexdigest()))
        with self.assertRaisesRegex(handoff.BuildError, "tracked modifications"):
            self.h.check_sdk()

    def test_game_preflight_does_not_require_quartus_or_cache(self):
        with self.available():
            result = self.h.preflight("game")
        self.assertEqual("passed", result["status"])
        self.assertEqual({"firmware"}, set(self.h.images))
        self.assertFalse(any("quartus_sh" in a or "sbt" in a for a in self.runner.commands))

    def test_missing_docker_is_actionable(self):
        with mock.patch.object(handoff.shutil, "which", return_value=None), self.assertRaisesRegex(handoff.BuildError, "Missing tool: docker"):
            self.h.docker()

    def test_low_disk_stops_before_quartus(self):
        with mock.patch.object(handoff.shutil, "disk_usage", return_value=type("Disk", (), {"free": 1024})()), self.assertRaisesRegex(handoff.BuildError, "Insufficient free disk"):
            self.h.check_quartus()
        self.assertFalse(self.runner.commands)

    def test_missing_device_is_fatal(self):
        self.runner.device = "No matching devices"
        with mock.patch.object(self.h, "disk"), self.assertRaisesRegex(handoff.BuildError, "device support"):
            self.h.check_quartus()

    def test_wrong_container_os_or_architecture_rejected(self):
        self.runner.image_arch = "arm64"
        with self.assertRaisesRegex(handoff.BuildError, "OS/architecture"):
            self.h.image("quartus")
        self.runner.image_arch = "amd64"
        self.runner.os_release = 'ID=debian\nVERSION_ID="13"'
        with mock.patch.object(self.h, "disk"), self.assertRaisesRegex(handoff.BuildError, "Ubuntu 22.04"):
            self.h.check_quartus()

    def test_containers_cannot_pull_or_download_and_sdk_is_read_only(self):
        command = self.h.container("firmware", ["make", "all"], self.h.pal, [(self.h.pal, False), (self.sdk, True)], ["CPATH="])
        self.assertIn("--pull=never", command)
        self.assertIn("--network=none", command)
        self.assertIn(str(self.sdk) + ":" + str(self.sdk) + ":ro", command)
        self.assertIn("sha256:" + "a" * 64, command)
        self.assertIn("/tmp:exec", command)
        self.assertIn("/handoff-home:exec,mode=1777", command)
        self.assertNotIn("build", command)
        self.assertNotIn("USE_QUARTUS_CONTAINER=0", command)

    def test_prepared_source_changes_rejected(self):
        with self.available():
            self.h.execute("prepare-core")
        (self.h.core / "README").write_text("changed")
        with self.assertRaisesRegex(handoff.BuildError, "source changed"):
            self.h.prepared()

    def test_snapshot_ignores_builds_and_rejects_source_edits(self):
        old = self.root / "pocket/build/old/app.elf"
        old.parent.mkdir(parents=True)
        old.write_bytes(b"stale")
        self.h.ensure_pal()
        self.assertFalse((self.h.pal / "pocket/build").exists())
        self.h.ensure_pal()
        (self.root / "sdlpal/main.c").write_text("edited")
        with self.assertRaisesRegex(handoff.BuildError, "PAL source changed"):
            self.h.ensure_pal()

    def test_stage_hash_prevents_stale_inputs(self):
        output = self.work / "core/new.bin"
        output.parent.mkdir(parents=True)
        output.write_bytes(b"first")
        self.h.state["stages"]["firmware"] = {"status": "passed", **self.h.artifacts([output])}
        self.h.require_stage("firmware")
        output.write_bytes(b"second")
        with self.assertRaisesRegex(handoff.BuildError, "changed/missing"):
            self.h.require_stage("firmware")

    def test_space_and_input_overlap_rejected_before_writes(self):
        self.h.work = self.base / "has space"
        with self.assertRaisesRegex(handoff.BuildError, "without spaces"):
            self.h.paths()
        self.h.work = self.sdk
        self.h.state_path = self.sdk / "handoff-state.json"
        with self.assertRaisesRegex(handoff.BuildError, "separate"):
            self.h.execute("game")
        self.assertFalse(self.h.state_path.exists())

    def test_runner_logs_exit_code_and_stops_failure(self):
        runner = handoff.Runner(self.work, "fixture")
        with self.assertRaisesRegex(handoff.BuildError, "Command failed"):
            runner.run([sys.executable, "-c", "print('failure detail'); raise SystemExit(7)"])
        records = [json.loads(x) for x in (self.work / "logs/commands.jsonl").read_text().splitlines()]
        self.assertEqual(7, records[0]["exit_code"])
        self.assertIn("failure detail", (self.work / records[0]["log"]).read_text())

    def test_firmware_first_command_failure_stops_before_os(self):
        self.h.core.mkdir(parents=True)
        archive = self.base / "musl.tar.gz"
        archive.write_bytes(b"fixture")
        self.args.musl_archive = archive
        self.runner.fail_if = lambda a: "make" in a and "install" in a
        with self.assertRaisesRegex(handoff.BuildError, "Injected"):
            self.h.firmware()
        self.assertFalse(any("os.bin" in a for a in self.runner.commands))

    def test_fpga_missing_stage_short_circuits_before_qsf_or_quartus(self):
        with self.assertRaisesRegex(handoff.BuildError, "firmware"):
            self.h.fpga()
        self.assertFalse(self.runner.commands)

    def test_cache_cannot_contain_or_be_inside_workspace(self):
        for cache in (self.work, self.work / "cache", self.base):
            cache.mkdir(parents=True, exist_ok=True)
            (cache / "cache-marker").write_text("cache")
            self.args.vexii_cache = cache
            with self.assertRaisesRegex(handoff.BuildError, "external input"):
                self.h.check_cache()

    def test_cache_requires_known_structure_and_ignores_private_home(self):
        cache = self.base / "cache-home"
        cache.mkdir()
        (cache / ".ssh").mkdir()
        (cache / ".ssh/id_rsa").write_text("fixture only")
        self.args.vexii_cache = cache
        with self.assertRaisesRegex(handoff.BuildError, "supported prewarmed"):
            self.h.check_cache()
        (cache / ".cache/coursier").mkdir(parents=True)
        (cache / ".cache/coursier/artifact.jar").write_text("fixture jar")
        self.assertEqual([".cache/coursier"], self.h.check_cache())
        self.h.core.mkdir(parents=True)
        self.runner.fail_if = lambda a: a[:2] == ["docker", "run"]
        with self.assertRaises(handoff.BuildError):
            self.h.netlist()
        self.assertTrue((self.h.core / "tools/.vexii-home/.cache/coursier/artifact.jar").exists())
        self.assertFalse((self.h.core / "tools/.vexii-home/.ssh").exists())

    def test_cache_rejects_symlinks_and_credentials(self):
        cache = self.base / "cache-home"
        jars = cache / ".cache/coursier"
        jars.mkdir(parents=True)
        self.args.vexii_cache = cache
        link = jars / "linked"
        link.symlink_to(self.sdk, target_is_directory=True)
        with self.assertRaisesRegex(handoff.BuildError, "symlink"):
            self.h.check_cache()
        link.unlink()
        (jars / "credentials").write_text("fixture")
        with self.assertRaisesRegex(handoff.BuildError, "credential"):
            self.h.check_cache()

    def test_snapshot_omits_signing_credentials(self):
        private = self.root / "sdlpal/scripts/sdlpal.key"
        private.parent.mkdir(parents=True)
        private.write_text("fixture only")
        self.h.ensure_pal()
        self.assertFalse((self.h.pal / "sdlpal/scripts/sdlpal.key").exists())

    def test_tool_image_change_requires_new_workspace(self):
        self.h.state["tool_images"] = {"firmware": {"id": "sha256:" + "b" * 64}}
        with self.assertRaisesRegex(handoff.BuildError, "image changed"):
            self.h.image("firmware")

    def test_edited_pal_commit_is_recorded_not_dependency_pin(self):
        (self.root / "sdlpal/main.c").write_text("int main(void) { return 1; }\n")
        git(self.root, "add", "sdlpal/main.c")
        git(self.root, "commit", "-qm", "user edit")
        with self.available():
            self.h.paths()
        self.assertEqual(git(self.root, "rev-parse", "HEAD"), self.h.state["pal_git_head"])
        self.assertIn("differs", self.h.state["pal_baseline_note"])

    def test_shell_metacharacters_rejected(self):
        for char in "&|()<>*?[]#{}!":
            self.h.work = self.base / ("unsafe" + char)
            with self.assertRaisesRegex(handoff.BuildError, "portable ASCII"):
                self.h.paths()

    def setup_fpga_body(self, fail=False):
        self.h.state["stages"].update(firmware={"status": "passed", "outputs": {}}, netlist={"status": "passed", "outputs": {}})
        target = self.h.core / handoff.TARGET
        target.mkdir(parents=True)
        (target / "ap_core.qsf").write_text("fixture source qsf")
        qsf = target / "bld/pal-handoff/ap_core.qsf"
        def hook(args):
            if "bld/pal-handoff/ap_core.qsf" in args:
                qsf.parent.mkdir(parents=True)
                qsf.write_text("set_global_assignment -name DEVICE 5CEBA4F23C8\nset_global_assignment -name VERILOG_FILE " + str(self.h.core) + "/pal_display_mode_video.v\nset_global_assignment -name VERILOG_FILE " + str(self.h.core) + "/VexiiRiscv_os25.v\n")
                qsf.with_suffix(".qpf").write_text("PROJECT_REVISION = ap_core")
                return "generated"
            if args and "quartus_map ap_core" in args[-1]:
                if fail:
                    raise handoff.BuildError("Injected Quartus flow failure")
                out = qsf.parent / "output_files"
                out.mkdir()
                for suffix in ("sof", "rbf", "map.rpt", "fit.rpt", "sta.rpt"):
                    (out / ("ap_core." + suffix)).write_bytes(b"\x01\x02")
                return "built fixture"
            return None
        self.runner.hook = hook
        return qsf

    def test_fpga_generates_local_qsf_and_stays_unreleased(self):
        qsf = self.setup_fpga_body()
        result = self.h.fpga()
        make = next(a for a in self.runner.commands if "bld/pal-handoff/ap_core.qsf" in a)
        self.assertIn("-o", make)
        self.assertIn("QPROCS=4", make)
        flow = next(a for a in self.runner.commands if a and "quartus_map ap_core" in a[-1])
        self.assertIn("&& quartus_fit ap_core && quartus_asm ap_core && quartus_sta ap_core", flow[-1])
        self.assertIn("--network=none", flow)
        self.assertIn("--pull=never", flow)
        self.assertFalse(result["release_ready"])
        self.assertEqual(b"\x80\x40", (qsf.parent / "output_files/os25.rbf_r").read_bytes())
        self.assertFalse((self.sdk / "runtime/MANIFEST").read_text().startswith("rebuilt"))
        self.assertFalse(any("package" in a or "sdk" in a or "full" in a for a in self.runner.commands))

    def test_fpga_failure_does_not_reverse_or_mark_output(self):
        qsf = self.setup_fpga_body(fail=True)
        with self.assertRaisesRegex(handoff.BuildError, "flow failure"):
            self.h.fpga()
        self.assertFalse((qsf.parent / "output_files/os25.rbf_r").exists())

    def test_report_rechecks_hashes_without_commands(self):
        output = self.work / "new.bin"
        output.parent.mkdir()
        output.write_bytes(b"first")
        self.h.state["stages"]["game"] = {"status": "passed", **self.h.artifacts([output])}
        self.h.save()
        self.assertEqual("passed", self.h.report()["artifact_integrity"])
        output.write_bytes(b"tampered")
        result = self.h.report()
        self.assertEqual("failed", result["artifact_integrity"])
        self.assertEqual("invalid_artifact", result["stages"]["game"]["status"])
        self.assertFalse(self.runner.commands)

    def test_relocated_script_plan_uses_new_location(self):
        tools = self.root / "pocket/tools"
        tools.mkdir()
        shutil.copy2(ROOT / "pocket/tools/build_handoff.py", tools / "build_handoff.py")
        new = self.base / "second-location"
        shutil.move(str(self.root), str(new))
        result = subprocess.run([sys.executable, str(new / "pocket/tools/build_handoff.py"), "plan", "--sdk", str(self.sdk), "--core", str(self.core), "--work", str(self.work)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(str(new), json.loads(result.stdout)["paths"]["source"])
        self.assertFalse(self.work.exists())


if __name__ == "__main__":
    unittest.main()
