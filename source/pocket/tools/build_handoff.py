#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline, fail-closed orchestration for the SDLPAL Pocket source handoff.

Uses already provisioned Docker images, never builds/pulls an image or installs
software. Container commands reproduce the pinned upstream wrappers' isolation
but deliberately bypass their auto-download and fallback behaviour. Paths in
this source/lock are relative; generated QSF paths belong to this local run.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
LOCK_PATH = ROOT / "pocket/build-lock.json"
STAGES = ("prepare-core", "game", "probe", "firmware", "netlist", "fpga")
VEXII = "src/fpga/vendor/vexriscv/VexiiRiscv"
TARGET = "src/fpga/targets/pocket"
FW = "src/firmware/os/bld/pocket"
EXCLUDED = {".git", "build", "__pycache__", ".pytest_cache", ".DS_Store"}
# Copy dependency caches, not an entire HOME (which can contain credentials).
CACHE_DIRS = (".sbt/boot", ".sbt/launchers", ".sbt/1.0/zinc", ".ivy2/cache",
              ".ivy2/local", ".cache/coursier", ".coursier/cache")


class BuildError(RuntimeError):
    pass


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def source_files(root):
    for dirname in ("sdlpal", "pocket"):
        for current, dirs, files in os.walk(root / dirname):
            dirs[:] = sorted(d for d in dirs if d not in EXCLUDED)
            for filename in sorted(files):
                path = Path(current) / filename
                if filename in EXCLUDED or filename.endswith((".pyc", ".o", ".d", ".key", ".pfx", ".p12", ".pem")):
                    continue
                if path.is_symlink():
                    raise BuildError(f"Source symlink not supported: {path}")
                yield path


def source_digest(root):
    h = hashlib.sha256()
    for path in source_files(root):
        h.update(path.relative_to(root).as_posix().encode() + b"\0")
        h.update(sha256(path).encode() + b"\n")
    return h.hexdigest()


class Runner:
    """One checked command at a time; a nonzero exit stops the stage."""
    def __init__(self, work, stage):
        self.work, self.stage, self.sequence = work, stage, 0
        self.stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")

    def run(self, args, cwd=None):
        self.sequence += 1
        logs = self.work / "logs"
        logs.mkdir(parents=True, exist_ok=True)
        name = f"{self.stamp}-{self.stage}-{self.sequence:03}.log"
        args = [str(x) for x in args]
        record = {"stage": self.stage, "argv": args, "cwd": str(cwd) if cwd else None,
                  "log": "logs/" + name, "started_utc": datetime.now(timezone.utc).isoformat()}
        env = os.environ.copy()
        # Prevent user Make overrides and Git network helpers from changing the plan.
        for key in ("MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "GIT_DIR", "GIT_WORK_TREE"):
            env.pop(key, None)
        env.update(GIT_TERMINAL_PROMPT="0", GIT_ALLOW_PROTOCOL="file", LC_ALL="C")
        try:
            result = subprocess.run(args, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, errors="replace")
            output, code = result.stdout, result.returncode
        except OSError as exc:
            output, code = str(exc), 127
        (logs / name).write_text("$ " + shlex.join(args) + "\n" + output)
        record.update(exit_code=code, finished_utc=datetime.now(timezone.utc).isoformat())
        with (logs / "commands.jsonl").open("a") as f:
            f.write(json.dumps(record) + "\n")
        if code:
            raise BuildError(f"Command failed ({code}): {shlex.join(args)}; see {logs / name}")
        return output.strip()


class Handoff:
    def __init__(self, args, root=ROOT, runner=None):
        self.args, self.root = args, Path(root).resolve()
        self.lock_path = self.root / "pocket/build-lock.json"
        self.lock = json.loads(self.lock_path.read_text())
        self.work = args.work.resolve()
        self.sdk, self.source_core = args.sdk.resolve(), args.core.resolve()
        self.core, self.pal = self.work / "core", self.work / "pal"
        self.state_path = self.work / "handoff-state.json"
        self.runner = runner or Runner(self.work, args.command)
        self.images = {}
        self.sources = {"core": self.source_core}
        self.sources["vexii"] = (args.vexii or self.source_core / VEXII).resolve()
        self.sources["spinal"] = (args.spinal or self.sources["vexii"] / "ext/SpinalHDL").resolve()
        self.sources["rvls"] = (args.rvls or self.sources["vexii"] / "ext/rvls").resolve()
        self.state = json.loads(self.state_path.read_text()) if self.state_path.exists() else {
            "schema": 1, "lock_sha256": sha256(self.lock_path), "stages": {},
            "release_ready": False, "hardware_verified": False,
            "runtime_policy": "SDK candidate unchanged; rebuilt outputs are a separate unverified set"}

    def save(self):
        write_json(self.state_path, self.state)

    def paths(self):
        # Upstream Make/QSF generators cannot safely quote whitespace/metacharacters.
        checked = [self.root, self.sdk, self.work, *self.sources.values()]
        checked += [p.resolve() for p in (self.args.musl_archive, self.args.vexii_cache, self.args.quartus_root, self.args.license_file) if p]
        for path in checked:
            if not re.fullmatch(r"[A-Za-z0-9_./-]+", str(path)):
                raise BuildError(f"Use portable ASCII local paths without spaces or shell/Make punctuation: {path}")
        for path in (self.root, self.sdk, *self.sources.values()):
            if self.work == path or self.work in path.parents or path in self.work.parents:
                raise BuildError(f"--work must be separate from every input tree: {path}")
        if self.state["lock_sha256"] != sha256(self.lock_path):
            raise BuildError("Build lock changed; choose a fresh --work directory")
        if sha256(self.root / self.lock["display_patch"]["path"]) != self.lock["display_patch"]["sha256"]:
            raise BuildError("Display patch SHA256 mismatch")
        if not shutil.which("git"):
            raise BuildError("Missing tool: git (install it yourself before retrying)")
        if (self.root / ".git").exists():
            # The port is intended to be edited and committed. Dependency HEADs
            # are strict, but PAL's exact working source is covered by its digest.
            status = self.git(self.root, "status", "--porcelain=v2", "--branch", "--untracked-files=no")
            match = re.search(r"(?m)^# branch.oid (.+)$", status)
            head = match[1] if match else "unknown"
            self.state["pal_git_head"] = head
            if head != self.lock["baseline_commit"]:
                self.state["pal_baseline_note"] = "PAL checkout HEAD differs from baseline; exact edited source is captured by pal_source_sha256"

    def git(self, path, *args):
        return self.runner.run(["git", "-C", path, *args])

    def pin(self, path, commit, name, clean=True):
        if not (path / ".git").exists():
            raise BuildError(f"{name} is uninitialized at {path}; supply its exact local pinned checkout")
        actual = self.git(path, "rev-parse", "HEAD")
        if actual != commit:
            raise BuildError(f"{name} pin mismatch: expected {commit}, found {actual}")
        if clean and self.git(path, "status", "--porcelain", "--untracked-files=no", "--ignore-submodules=all"):
            raise BuildError(f"{name} has tracked modifications; supply a pristine pinned checkout")

    def check_sdk(self):
        self.pin(self.sdk, self.lock["sdk"]["commit"], "SDK")
        # Do not trust a hand-edited MANIFEST merely because it matches its files.
        if self.git(self.sdk, "status", "--porcelain", "--", "src/sdk", "runtime"):
            raise BuildError("SDK source/runtime changed; never mix candidate and rebuilt runtime")
        manifest = (self.sdk / "runtime/MANIFEST").read_text()
        if f"source: {self.lock['sdk']['runtime_source']}" not in manifest:
            raise BuildError("SDK runtime source MANIFEST mismatch")
        records = {}
        for line in manifest.splitlines():
            if line and not line.startswith("#"):
                digest, name = line.split(maxsplit=1)
                records[name.removeprefix("./")] = digest
        verified = {}
        for name in ("pocket/os25.rbf_r", "pocket/os.bin", "pocket/loader.bin"):
            path = self.sdk / "runtime" / name
            if not path.is_file() or hashlib.md5(path.read_bytes()).hexdigest() != records.get(name):
                raise BuildError(f"SDK runtime checksum mismatch: {name}")
            verified[name] = sha256(path)
        for name in ("libc.a", "libm.a", "crt1.o", "crti.o", "crtn.o"):
            self.require_file(self.sdk / "src/sdk/musl/lib" / name)
        return verified

    def check_sources(self):
        self.pin(self.source_core, self.lock["core"]["commit"], "Core")
        for sub in self.lock["submodules"]:
            source = self.sources[sub["name"]]
            self.pin(source, sub["commit"], sub["name"])
            entry = self.git(self.sources[sub["parent"]], "ls-tree", "HEAD", sub["gitlink"])
            if not entry.startswith("160000 commit " + sub["commit"] + "\t"):
                raise BuildError(f"{sub['name']} gitlink does not match locked parent")
        self.require_file(self.sources["vexii"] / "build.sbt")
        self.require_file(self.sources["spinal"] / "build.sbt")

    @staticmethod
    def require_file(path):
        if not path.is_file() or not path.stat().st_size:
            raise BuildError(f"Missing/empty prerequisite: {path}")

    def prepared(self):
        stage = self.state["stages"].get("prepare-core", {})
        if stage.get("status") != "passed":
            raise BuildError("Run prepare-core successfully in this --work directory first")
        self.pin(self.core, self.lock["core"]["commit"], "Prepared Core", clean=False)
        for sub in self.lock["submodules"]:
            self.pin(self.core / sub["path"], sub["commit"], "Prepared " + sub["name"])
        self.require_file(self.core / "PAL_DISPLAY_MODES_SOURCE_ONLY.json")
        marker = json.loads((self.core / "PAL_DISPLAY_MODES_SOURCE_ONLY.json").read_text())
        if marker.get("patch_sha256") != self.lock["display_patch"]["sha256"]:
            raise BuildError("Prepared core patch provenance mismatch")
        if self.tracked_core_digest() != stage.get("source_sha256"):
            raise BuildError("Prepared core source changed; use a fresh work directory")

    def tracked_core_digest(self):
        h = hashlib.sha256()
        for path in self.git(self.core, "ls-files").splitlines():
            file = self.core / path
            if file.is_file():
                h.update(path.encode() + b"\0" + sha256(file).encode())
        # Added source is untracked relative to the upstream baseline.
        file = self.core / TARGET / "pal_display_mode_video.v"
        self.require_file(file)
        h.update(sha256(file).encode())
        return h.hexdigest()

    def disk(self):
        existing = self.work
        while not existing.exists():
            existing = existing.parent
        free = shutil.disk_usage(existing).free
        minimum = self.lock["core"]["minimum_free_gib"] * 1024**3
        if free < minimum:
            raise BuildError(f"Insufficient free disk: {free / 1024**3:.1f} GiB; require at least {minimum / 1024**3:.0f} GiB in work filesystem (build-only reserve)")
        return {"free_gib": round(free / 1024**3, 1), "minimum_free_gib": minimum // 1024**3}

    def docker(self):
        if platform.system() not in ("Linux", "Darwin"):
            raise BuildError("Use Linux or macOS with a local Linux Docker daemon; native Windows flow is not supported")
        if not shutil.which("docker"):
            raise BuildError("Missing tool: docker; provision it yourself (no automatic install)")
        data = json.loads(self.runner.run(["docker", "info", "--format", "{{json .}}"] ))
        if data.get("OSType") != "linux":
            raise BuildError("A Linux Docker daemon is required")
        endpoint = self.runner.run(["docker", "context", "inspect", "--format", "{{.Endpoints.docker.Host}}"])
        if not endpoint.startswith("unix://") or os.environ.get("DOCKER_HOST", "unix://").startswith(("tcp:", "ssh:")):
            raise BuildError("Only a local Docker daemon with local bind mounts is supported")

    def image(self, kind):
        if kind in self.images:
            return self.images[kind]
        requested = getattr(self.args, kind + "_image") or self.lock["images"][kind]
        data = json.loads(self.runner.run(["docker", "image", "inspect", requested]))[0]
        if data.get("Os") != "linux" or (kind == "quartus" and data.get("Architecture") != "amd64"):
            raise BuildError(f"Unsupported {kind} image OS/architecture; Quartus requires linux/amd64")
        ident = data.get("Id", "")
        if not re.fullmatch(r"sha256:[0-9a-f]{64}", ident):
            raise BuildError("Docker image inspection did not return a content-addressed image ID")
        previous = self.state.get("tool_images", {}).get(kind, {}).get("id")
        if previous and previous != ident:
            raise BuildError(f"{kind} image changed within workspace; use a fresh --work to prevent stale mixed-tool objects")
        self.images[kind] = ident
        self.state.setdefault("tool_images", {})[kind] = {"requested": requested, "id": ident}
        return ident

    def container(self, kind, command, cwd=None, mounts=(), env=(), cache=None):
        # Equivalent isolation to the pinned wrappers, with pulling and networking disabled.
        args = ["docker", "run", "--rm", "--pull=never", "--network=none",
                "--user", f"{os.getuid()}:{os.getgid()}", "--tmpfs", "/tmp:exec",
                "--tmpfs", "/handoff-home:exec,mode=1777", "-e", "HOME=/handoff-home"]
        if kind == "quartus":
            args += ["--platform", "linux/amd64"]
        for source, readonly in mounts:
            args += ["-v", f"{source}:{source}" + (":ro" if readonly else "")]
        if cache:
            args += ["-v", f"{cache}:/vexiihome", "-e", "HOME=/vexiihome",
                     "-e", "SBT_OPTS=-Dsbt.offline=true"]
        for value in env:
            args += ["-e", value]
        if kind == "quartus" and self.args.quartus_root:
            qroot = self.args.quartus_root.resolve()
            args += ["-v", f"{qroot}:{qroot}:ro", "-e", f"QUARTUS_ROOTDIR={qroot}",
                     "-e", f"PATH={qroot}/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"]
        if self.args.license_file and kind == "quartus":
            license_file = self.args.license_file.resolve()
            args += ["-v", f"{license_file}:{license_file}:ro", "-e", f"LM_LICENSE_FILE={license_file}"]
        args += ["-w", str(cwd or "/tmp"), self.image(kind), *map(str, command)]
        return args

    def tool_check(self, kind, commands):
        text = "set -eu; " + "; ".join("command -v " + shlex.quote(c) for c in commands)
        self.runner.run(self.container(kind, ["bash", "-c", text]))

    def check_quartus(self):
        self.disk()
        if self.args.quartus_root:
            if platform.system() != "Linux":
                raise BuildError("Quartus bind-mount mode is supported on Linux only")
            for name in ("quartus_sh", "quartus_map", "quartus_fit", "quartus_asm", "quartus_sta"):
                self.require_file(self.args.quartus_root.resolve() / "bin" / name)
        if self.args.license_file:
            self.require_file(self.args.license_file.resolve())
        self.tool_check("quartus", ["quartus_sh", "quartus_map", "quartus_fit", "quartus_asm", "quartus_sta"])
        os_text = self.runner.run(self.container("quartus", ["cat", "/etc/os-release"]))
        info = dict(line.split("=", 1) for line in os_text.splitlines() if "=" in line)
        if info.get("ID", "").strip('"') != self.lock["core"]["container_os"] or info.get("VERSION_ID", "").strip('"') != self.lock["core"]["container_os_version"]:
            raise BuildError("Quartus container must use pinned upstream Ubuntu 22.04 environment")
        version = self.runner.run(self.container("quartus", ["quartus_sh", "--version"]))
        if not re.search(r"Version\s+25\.1(?:std)?(?:[.\s]|$)", version):
            raise BuildError("Require existing Quartus Prime 25.1 Lite/Standard")
        if not re.search(r"\b(Lite|Standard)\b", version, re.I):
            raise BuildError("Quartus edition is not Lite/Standard")
        # No license is installed or accepted here; an optional existing local license
        # file is mounted only when explicitly requested. Network licenses are disabled.
        probe = self.work / "logs/check-device.tcl"
        probe.parent.mkdir(parents=True, exist_ok=True)
        device = self.lock["core"]["device"]
        probe.write_text('load_package device\nif {[lsearch -exact [get_part_list -family "Cyclone V"] "' + device + '"] < 0} {error "Required device unavailable"}\nputs "HANDOFF_DEVICE=' + device + '"\n')
        output = self.runner.run(self.container("quartus", ["quartus_sh", "-t", probe], mounts=[(probe, True)]))
        if "HANDOFF_DEVICE=" + device not in output:
            raise BuildError("Quartus cannot verify Cyclone V " + device + " device support")
        self.state["quartus_version_output"] = version

    def check_musl(self):
        archive = self.args.musl_archive
        if archive is None:
            raise BuildError("Provide --musl-archive with local musl-1.2.5.tar.gz; no downloads are performed")
        archive = archive.resolve()
        self.require_file(archive)
        if sha256(archive) != self.lock["musl"]["sha256"]:
            raise BuildError("musl archive SHA256 mismatch")

    def check_cache(self):
        cache = self.args.vexii_cache
        if cache is None or not cache.is_dir():
            raise BuildError("Provide --vexii-cache with a prewarmed local sbt/Scala dependency cache; cold/offline resolution cannot succeed")
        cache = cache.resolve()
        if cache == self.work or self.work in cache.parents or cache in self.work.parents:
            raise BuildError("--vexii-cache must be a separate external input, never an ancestor/descendant of work")
        entries = [name for name in CACHE_DIRS if (cache / name).is_dir() and any((cache / name).iterdir())]
        if not entries:
            raise BuildError("No supported prewarmed cache directories found; expected " + ", ".join(CACHE_DIRS))
        # Do not follow links out of an input cache or copy credential files.
        for name in entries:
            entry = cache / name
            if entry.is_symlink() or any(p.is_symlink() for p in entry.parents if p != cache and cache in p.parents):
                raise BuildError("Cache symlinks are unsupported; provide actual dedicated cache directories")
            for current, dirs, files in os.walk(entry):
                for item in dirs + files:
                    path = Path(current) / item
                    if path.is_symlink() or re.search(r"credential|password|secret|token|^\.netrc$", item, re.I):
                        raise BuildError("Cache contains a symlink or credential-like file; provide a sanitized dedicated cache")
        return entries

    def preflight(self, stage, require_prepared=True):
        self.paths()
        if stage in ("prepare-core", "all"):
            self.check_sources()
        if stage in ("game", "probe", "all"):
            self.check_sdk()
        if stage in ("firmware", "netlist", "fpga") and require_prepared:
            self.prepared()
        if stage != "prepare-core":
            self.docker()
        if stage in ("game", "probe", "firmware", "fpga", "all"):
            self.tool_check("firmware", ["make", "python3", "riscv64-unknown-elf-gcc", "riscv64-unknown-elf-objcopy", "riscv64-unknown-elf-size", "hexdump", "bash", "tar", "sed", "awk"])
        if stage in ("firmware", "all"):
            self.check_musl()
        if stage in ("netlist", "all"):
            self.check_cache()
            self.tool_check("vexii", ["bash", "git", "java", "sbt", "perl"])
        if stage in ("fpga", "all"):
            self.check_quartus()
        return {"status": "passed", "stage": stage, "build_executed": False,
                "note": "Availability checks only; offline cache completeness is proven by netlist execution"}

    def ensure_pal(self):
        digest = source_digest(self.root)
        if self.pal.exists():
            if self.state.get("pal_source_sha256") != digest or source_digest(self.pal) != digest:
                raise BuildError("PAL source changed or workspace is unowned; choose a fresh --work directory")
        else:
            for source in source_files(self.root):
                output = self.pal / source.relative_to(self.root)
                output.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, output)
            self.state["pal_source_sha256"] = digest
            self.save()

    def prepare_core(self):
        if self.core.exists():
            raise BuildError("Prepared core already exists; choose a fresh --work directory (never overwritten)")
        helper = self.root / "pocket/fpga/prepare_runtime.py"
        self.runner.run([sys.executable, helper, "--source-core", self.source_core, "--output", self.core])
        for sub in self.lock["submodules"]:
            output = self.core / sub["path"]
            # A Git checkout leaves empty gitlink directories. clone accepts those.
            self.runner.run(["git", "clone", "--no-local", "--no-checkout", self.sources[sub["name"]], output])
            self.git(output, "checkout", "--detach", sub["commit"])
            self.pin(output, sub["commit"], sub["name"])
        return {"source_sha256": self.tracked_core_digest(), "outputs": {}}

    def require_stage(self, stage):
        record = self.state["stages"].get(stage, {})
        if record.get("status") != "passed":
            raise BuildError(f"Required stage {stage} has not passed in this workspace")
        for path, digest in record.get("outputs", {}).items():
            file = self.work / path
            if not file.is_file() or sha256(file) != digest:
                raise BuildError(f"{stage} output changed/missing: {path}; rebuild that stage")

    def artifacts(self, paths):
        result = {}
        for path in paths:
            self.require_file(path)
            result[path.relative_to(self.work).as_posix()] = sha256(path)
        return {"outputs": result}

    def app(self, probe=False):
        self.ensure_pal()
        name = "probe" if probe else "game"
        build = "build/" + name
        args = ["make", "--no-print-directory", "all", "SDK_ROOT=" + str(self.sdk),
                "BUILD_DIR=" + build, "OBJ_DIR=" + build + "/obj", "USE_SDK_CONTAINER=0",
                "OF_SDK_IN_CONTAINER=1", "CROSS=riscv64-unknown-elf-"]
        if probe:
            ogg = sorted("../sdlpal/liboggvorbis/src/" + p.name for p in (self.pal / "sdlpal/liboggvorbis/src").glob("*.c"))
            args += ["SRCS=" + " ".join(["probe.c", "files.c", "audio_stream.c", *ogg]),
                     "LDFLAGS=-Wl,-Map,build/probe/pal-probe.map -Wl,--wrap=fopen,--wrap=access"]
        self.runner.run(self.container("firmware", args, self.pal / "pocket",
                                      [(self.pal, False), (self.sdk, True)], ["CPATH=", "OF_SDK_IN_CONTAINER=1"]))
        return self.artifacts([self.pal / "pocket" / build / "app.elf"])

    def firmware(self):
        archive = self.core / "src/firmware/musl/build" / self.lock["musl"]["filename"]
        archive.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(self.args.musl_archive.resolve(), archive)
        for target in ("install", "os.bin"):
            self.runner.run(self.container("firmware", ["make", target, "TARGET=pocket"],
                                          self.core / "src/firmware/os", [(self.core, False)],
                                          ["CPATH=/usr/lib/picolibc/riscv64-unknown-elf/include"]))
        verifier = load_module(self.root / "pocket/fpga/verify_boot_image.py", "handoff_boot_verifier")
        result = verifier.verify_boot_image(self.core / FW / "firmware.mif", self.core / FW / "boot.bin")
        if sha256(self.core / FW / "firmware.mif") != sha256(self.core / TARGET / "firmware.mif"):
            raise BuildError("Installed boot MIF differs from freshly built firmware MIF")
        artifacts = self.artifacts([self.core / FW / name for name in ("firmware.mif", "boot.bin", "os.bin")] + [self.core / TARGET / "firmware.mif"])
        artifacts["boot_verification"] = result
        return artifacts

    def netlist(self):
        cache = self.core / "tools/.vexii-home"
        if not cache.exists():
            for name in self.check_cache():
                output = cache / name
                output.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(self.args.vexii_cache.resolve() / name, output)
            self.state["vexii_cache_policy"] = "Only allowlisted dependency caches copied; HOME/configuration/credentials excluded"
        output = self.core / VEXII / "VexiiRiscv_os25.v"
        if output.exists():
            output.unlink()  # Only our own generated output; cannot reuse a stale success.
        self.runner.run(self.container("vexii", ["bash", "../generate_vexii.sh", "os25"],
                                      self.core / VEXII, [(self.core, False)], cache=cache))
        return self.artifacts([output])

    def fpga(self):
        self.require_stage("firmware")
        self.require_stage("netlist")
        target = self.core / TARGET
        # Generate source paths locally using the pinned Makefile's actual rule.
        # Prerequisites have already passed, so no automatic cpu/submodule targets
        # are permitted. -o marks those exact file targets as already made.
        self.require_file(target / "ap_core.qsf")
        qsf = target / "bld/pal-handoff/ap_core.qsf"
        if qsf.exists():
            raise BuildError("FPGA job already exists; use a new --work directory for another fit")
        netlist = "../../vendor/vexriscv/VexiiRiscv/VexiiRiscv_os25.v"
        command = ["make", "--no-print-directory", "-o", netlist,
                   "bld/pal-handoff/ap_core.qsf", "TARGET=pocket", "VARIANT=os25", "JOB=pal-handoff",
                   "QPROCS=" + str(self.lock["core"]["qprocs"])]
        self.runner.run(self.container("firmware", command, target, [(self.core, False)]))
        self.require_file(qsf)
        text = qsf.read_text()
        if not re.search(r"-name\s+DEVICE\s+" + self.lock["core"]["device"] + r"\b", text):
            raise BuildError("Generated QSF device mismatch")
        if str(self.core) not in text or "pal_display_mode_video.v" not in text or "VexiiRiscv_os25.v" not in text:
            raise BuildError("Generated QSF is missing local source paths/display patch/os25 CPU")
        flow = ["bash", "-c", "set -eu; quartus_map ap_core && quartus_fit ap_core && quartus_asm ap_core && quartus_sta ap_core"]
        self.runner.run(self.container("quartus", flow, qsf.parent, [(self.core, False)]))
        out = qsf.parent / "output_files"
        paths = [out / ("ap_core." + suffix) for suffix in ("sof", "rbf", "map.rpt", "fit.rpt", "sta.rpt")]
        self.artifacts(paths)
        # Bit reversal is a deterministic file transform, equivalent to upstream reverse_bits.c.
        reversed_file = out / "os25.rbf_r"
        table = bytes(int(f"{x:08b}"[::-1], 2) for x in range(256))
        reversed_file.write_bytes((out / "ap_core.rbf").read_bytes().translate(table))
        paths += [reversed_file, qsf, qsf.parent / "ap_core.qpf"]
        return {**self.artifacts(paths), "release_ready": False,
                "review_required": ["timing closure and unconstrained paths", "resource fit", "Pocket and Dock acceptance", "coherent runtime packaging"]}

    def execute(self, stage):
        self.paths()  # Validate output ownership before creating any state/log file.
        self.state["stages"][stage] = {"status": "running"}
        # Invalidate dependent successes before rebuilding a prerequisite.
        if stage in ("prepare-core", "firmware", "netlist"):
            self.state["stages"].pop("fpga", None)
        self.save()
        try:
            self.preflight(stage)
            result = {"prepare-core": self.prepare_core, "game": self.app,
                      "probe": lambda: self.app(True), "firmware": self.firmware,
                      "netlist": self.netlist, "fpga": self.fpga}[stage]()
            self.state["stages"][stage] = {"status": "passed", **result}
        except (BuildError, OSError, ValueError) as exc:
            self.state["stages"][stage] = {"status": "failed", "error": str(exc)}
            self.save()
            raise BuildError(str(exc)) from exc
        self.save()
        return self.state["stages"][stage]

    def report(self):
        if not self.state_path.exists():
            raise BuildError("No handoff-state.json exists in this workspace")
        result = json.loads(json.dumps(self.state))
        invalid = []
        for stage, record in result["stages"].items():
            if record.get("status") != "passed":
                continue
            try:
                self.require_stage(stage)
            except BuildError as exc:
                record["status"] = "invalid_artifact"
                record["error"] = str(exc)
                invalid.append(stage)
        if self.state.get("pal_source_sha256") and (not self.pal.is_dir() or source_digest(self.pal) != self.state["pal_source_sha256"]):
            invalid.append("pal_source_snapshot")
        result["artifact_integrity"] = "failed" if invalid else "passed"
        result["invalid"] = invalid
        result["report_note"] = "Recorded stage outcomes plus current output hashes; no timing or hardware acceptance is implied"
        return result

    def plan(self):
        return {"status": "PLAN_ONLY_NO_COMMANDS_EXECUTED", "paths": {
            "source": str(self.root), "sdk_read_only": str(self.sdk), "core_input": str(self.source_core),
            "work": str(self.work), "prepared_core": str(self.core), "pal_snapshot": str(self.pal)},
            "pins": self.lock, "order": ["preflight --stage game", "game", "probe", "prepare-core", "firmware", "netlist", "fpga", "report"],
            "commands": {
                "prepare-core": "python3 pocket/fpga/prepare_runtime.py --source-core CORE --output WORK/core; local git clone + detached exact submodule checkouts",
                "game": "offline firmware container: make all BUILD_DIR=build/game SDK_ROOT=SDK (SDK read-only)",
                "probe": "offline firmware container: make all BUILD_DIR=build/probe SRCS=probe.c, files.c, audio_stream.c, Ogg/Vorbis",
                "firmware": "offline firmware container: make install TARGET=pocket; make os.bin TARGET=pocket; verify_boot_image",
                "netlist": "offline vexii container: bash ../generate_vexii.sh os25 (copied prewarmed cache)",
                "fpga": "generate bld/pal-handoff/ap_core.qsf with pinned Makefile; isolated offline Quartus 25.1 container: map && fit && asm && sta; reverse RBF bits",
                "report": "read handoff-state.json and recheck output hashes; no synthesis and no package/SD write"},
            "network": "none", "pull_policy": "never", "release_ready": False,
            "note": "Fresh local image IDs are captured in execution reports; tags alone do not claim binary reproducibility. No install, EULA acceptance, upload, runtime publication or SD write."}


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("command", choices=("plan", "preflight", *STAGES, "report"))
    p.add_argument("--sdk", type=Path, required=True, help="Existing exact pinned SDK checkout; always read-only")
    p.add_argument("--core", type=Path, required=True, help="Existing clean exact pinned upstream Core checkout")
    p.add_argument("--work", type=Path, required=True, help="Separate local build workspace; never an SD-card path")
    for name in ("vexii", "spinal", "rvls"):
        p.add_argument("--" + name, type=Path, help="Existing local pinned checkout; otherwise use core's submodule path")
    p.add_argument("--stage", choices=(*STAGES, "all"), default="all", help="Scope of preflight (default: all)")
    for kind in ("firmware", "vexii", "quartus"):
        p.add_argument("--" + kind + "-image", help="Existing local Docker image; default is recorded in build-lock.json")
    p.add_argument("--quartus-root", type=Path, help="Optional existing Linux Quartus install's quartus directory, mounted read-only with a prepared bind image")
    p.add_argument("--license-file", type=Path, help="Optional already accepted local license, mounted read-only; no network license servers")
    p.add_argument("--musl-archive", type=Path, help="Local pinned musl-1.2.5.tar.gz, required for firmware")
    p.add_argument("--vexii-cache", type=Path, help="Local prewarmed sbt HOME cache, copied into workspace; required for netlist")
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        handoff = Handoff(args)
        if args.command == "plan":
            result = handoff.plan()
        elif args.command == "report":
            result = handoff.report()
        elif args.command == "preflight":
            result = handoff.preflight(args.stage)
            write_json(handoff.work / "preflight.json", result)
        else:
            result = handoff.execute(args.command)
        print(json.dumps(result, indent=2, sort_keys=True))
        return 1 if result.get("artifact_integrity") == "failed" else 0
    except (BuildError, OSError, ValueError) as exc:
        print("BLOCKED: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
