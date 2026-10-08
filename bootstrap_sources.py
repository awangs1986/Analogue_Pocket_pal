#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify and materialize the source handoff with pinned local dependencies, without networking.

Distributed both here (for review) and at the handoff ZIP root (for use).
No tool installation, image pull, license acceptance, build or device access.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys

DEPENDENCIES = ("openfpgaSDK", "openfpgaCore", "VexiiRiscv", "SpinalHDL", "rvls")


def contained(root, relative):
    name = PurePosixPath(relative)
    if name.is_absolute() or not name.parts or any(p in ("..", ".") for p in name.parts):
        raise ValueError(f"Unsafe archive path: {relative!r}")
    path = root.joinpath(*name.parts)
    if root not in path.resolve().parents or path.is_symlink():
        raise ValueError(f"Escaping or symbolic-link path: {relative!r}")
    return path


def verify(root):
    root = Path(root).resolve()
    manifest = json.loads((root / "handoff-manifest.json").read_text())
    if manifest.get("schema") != 1:
        raise ValueError("Unsupported handoff manifest schema")
    for name, expected in manifest["files"].items():
        path = contained(root, name)
        if not path.is_file():
            raise ValueError(f"Missing file: {name}")
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f"SHA-256 mismatch: {name}")
    if not any(name.startswith("source/") for name in manifest["files"]):
        raise ValueError("Source inventory is empty")
    for name in DEPENDENCIES:
        entry = manifest["repositories"][name]
        if not entry.get("external") and entry["bundle"] not in manifest["files"]:
            raise ValueError(f"Unverified bundle: {name}")
        commit = entry["commit"]
        if len(commit) != 40 or any(c not in "0123456789abcdef" for c in commit):
            raise ValueError(f"Invalid pinned commit: {name}")
        if entry.get("shallow"):
            if entry["shallow"] not in manifest["files"]:
                raise ValueError(f"Unverified shallow boundary file: {name}")
            boundaries = contained(root, entry["shallow"]).read_text().splitlines()
            if not boundaries or any(len(c) != 40 or any(x not in "0123456789abcdef" for x in c) for c in boundaries):
                raise ValueError(f"Invalid shallow boundaries: {name}")
    if "dependencies/musl-1.2.5.tar.gz" not in manifest["files"]:
        raise ValueError("Missing musl source archive")
    return manifest


def materialize(root, workspace, external=None):
    root = Path(root).resolve()
    workspace = Path(workspace).resolve()
    manifest = verify(root)
    if workspace.exists():
        raise ValueError("Workspace must not exist; choose a new directory")
    if root == workspace or workspace in root.parents:
        raise ValueError("Workspace must not contain the input package")
    if shutil.which("git") is None:
        raise ValueError("git is required; no software is installed automatically")
    external = external or {}
    verified_external = {}
    # Verify caller-provided repositories before creating any destination.
    for name in DEPENDENCIES:
        entry = manifest["repositories"][name]
        if not entry.get("external"):
            continue
        if name not in external or external[name] is None:
            raise ValueError(f"{name} is external: supply its pinned local checkout; see DEPENDENCIES.md")
        checkout = Path(external[name]).resolve()
        if not checkout.is_dir():
            raise ValueError(f"Missing local dependency checkout: {name}")
        actual = subprocess.check_output(["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
        if actual != entry["commit"]:
            raise ValueError(f"External dependency must be checked out at exact pin: {name}")
        if subprocess.check_output(["git", "-C", str(checkout), "status", "--porcelain"], text=True).strip():
            raise ValueError(f"External dependency checkout must be clean: {name}")
        verified_external[name] = checkout
    workspace.mkdir(parents=True)
    logs = workspace / "logs"
    logs.mkdir()
    with (logs / "bootstrap.log").open("w") as log:
        def run(*args):
            print("RUN", json.dumps(args), file=log, flush=True)
            return subprocess.run(args, check=True, text=True, stdout=log, stderr=log)

        for name in DEPENDENCIES:
            entry = manifest["repositories"][name]
            dest = workspace / "deps" / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            if entry.get("external"):
                # File transport only; no remote fetch, installation or credentials.
                run("git", "-c", "protocol.allow=never", "-c", "protocol.file.allow=always",
                    "clone", "--no-local", "--no-checkout", str(verified_external[name]), str(dest))
            else:
                bundle = contained(root, entry["bundle"])
                run("git", "clone", "--no-checkout", str(bundle), str(dest))
            if entry.get("shallow"):
                shutil.copy2(contained(root, entry["shallow"]), dest / ".git/shallow")
            run("git", "-C", str(dest), "fsck", "--connectivity-only", "--no-dangling")
            run("git", "-C", str(dest), "checkout", "--detach", entry["commit"])
            actual = subprocess.check_output(["git", "-C", str(dest), "rev-parse", "HEAD"], text=True).strip()
            if actual != entry["commit"]:
                raise ValueError(f"Checkout pin mismatch: {name}")
        target = workspace / "mister-pal-pocket"
        target.mkdir()
        # Export rather than Git history: the original public repository has
        # Windows signing-key material irrelevant to the Pocket/MiSTer build.
        # The handoff inventory records its baseline without redistributing it.
        for name in sorted(manifest["files"]):
            if not name.startswith("source/"):
                continue
            relative = name[len("source/"):]
            if ".git" in PurePosixPath(relative).parts:
                raise ValueError("Source overlay must not contain Git metadata")
            dest = contained(target, relative)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(contained(root, name), dest)
        shutil.copy2(root / "dependencies/musl-1.2.5.tar.gz", workspace / "deps/musl-1.2.5.tar.gz")
        shutil.copy2(root / "handoff-manifest.json", workspace / "source-handoff-manifest.json")
        print("PASS: sources materialized; no build or device action performed", file=log)
    return target


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=Path(__file__).resolve().parent,
                        help="Unpacked handoff root (default: directory of this script)")
    parser.add_argument("--workspace", type=Path, help="New destination directory")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--sdk", type=Path, help="Clean local openfpgaSDK checkout at the manifest pin")
    parser.add_argument("--core", type=Path, help="Clean local openfpgaCore checkout at the manifest pin")
    parser.add_argument("--spinal", type=Path, help="Clean local SpinalHDL checkout at the manifest pin")
    args = parser.parse_args(argv)
    try:
        if args.verify_only:
            manifest = verify(args.package)
            print(f"PASS: {len(manifest['files'])} file SHA-256 checks")
        elif args.workspace:
            print(materialize(args.package, args.workspace, {"openfpgaSDK": args.sdk, "openfpgaCore": args.core, "SpinalHDL": args.spinal}))
            print("Sources ready. Read README_构建交接.md before building.")
        else:
            parser.error("--workspace is required unless --verify-only is selected")
    except (ValueError, OSError, KeyError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        print("Any partial workspace is retained; see its logs/bootstrap.log.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
