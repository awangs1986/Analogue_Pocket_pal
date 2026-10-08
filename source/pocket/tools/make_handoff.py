#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble a unified source handoff from verified local inputs; no downloads."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PINS = {
    "mister_pal": "1d35d896b99722b9968685c1284cb5f1566cd662",
    "openfpgaSDK": "a408ddc12aed0dfaa4aa22c06af82f829db77126",
    "openfpgaCore": "618a3eb985759a4154115109c2c8036271252888",
    "VexiiRiscv": "580b76c3868512c8316bb7a3d3add81cad49a0dc",
    "SpinalHDL": "6f8510cdbb8ad7b8bcc0f6d58395669c4c4d7e2d",
    "rvls": "94f850e5f7c9a36e5aceb3810d658fffae55b497",
}
MUSL_SHA256 = "a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4"
SENSITIVE_NAME = re.compile(r"(?:^|/)(?:\.env(?:\.[^/]*)?|id_rsa|id_ed25519|credentials)(?:/|$)|\.(?:key|pfx|p12|pem|keystore|jks)$", re.I)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy_file(src, dst):
    if src.is_symlink() or not src.is_file():
        raise ValueError(f"Expected regular file: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def assemble(args):
    output = args.output.resolve()
    if output.exists():
        raise ValueError("Output directory already exists; preserve it and choose a new one")
    if ROOT == output or ROOT in output.parents:
        raise ValueError("Output must be outside the source repository")
    if args.zip.exists():
        raise ValueError("ZIP already exists; choose a new filename")
    local = {"mister_pal": ROOT, "openfpgaSDK": args.sdk,
             "openfpgaCore": args.core, "VexiiRiscv": args.vexii,
             "SpinalHDL": args.spinal, "rvls": args.rvls}
    for name, path in local.items():
        head = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        if head != PINS[name]:
            raise ValueError(f"Pinned HEAD mismatch for {name}: {head}")
    if sha(args.musl) != MUSL_SHA256:
        raise ValueError("musl source archive hash mismatch")
    output.mkdir(parents=True)
    names = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "--cached", "--others", "--exclude-standard", "-z"])
    omitted = []
    for name in sorted(set(names.decode().split("\0")) - {""}):
        parts = Path(name).parts
        if any(p in (".git", "__pycache__", ".pytest_cache") for p in parts) or name.startswith("pocket/build/"):
            raise ValueError(f"Unexpected generated/private source input: {name}")
        if SENSITIVE_NAME.search(name):
            omitted.append(name)
            continue
        copy_file(ROOT / name, output / "source" / name)
    for name in ("README_构建交接.md", "BUILD_SPEC.md"):
        copy_file(ROOT / name, output / name)
        # These two convenience copies sit above source/, unlike their
        # repo-local originals. Keep their Markdown documentation links valid.
        top = output / name
        top.write_text(top.read_text().replace("(pocket/", "(source/pocket/"))
    copy_file(ROOT / "pocket/tools/bootstrap_sources.py", output / "bootstrap_sources.py")
    (output / "dependencies").mkdir()
    repositories = {}
    for name, path in local.items():
        if name == "mister_pal":
            continue  # Never redistribute signing material in original history.
        object_names = subprocess.check_output(["git", "-C", str(path), "rev-list", "--objects", "HEAD"], text=True)
        for line in object_names.splitlines():
            historical_name = line.partition(" ")[2]
            if SENSITIVE_NAME.search(historical_name):
                raise ValueError(f"Dependency history needs a sensitive-file review: {name}/{historical_name}")
        filename = name + ".bundle"
        bundle = output / "dependencies" / filename
        subprocess.run(["git", "-C", str(path), "bundle", "create", str(bundle), "HEAD"], check=True)
        heads = subprocess.check_output(["git", "bundle", "list-heads", str(bundle)], text=True)
        if heads.strip() != PINS[name] + " HEAD":
            raise ValueError(f"Unexpected bundle contents: {name}")
        repositories[name] = {"bundle": "dependencies/" + filename, "commit": PINS[name]}
        shallow_path = Path(subprocess.check_output(
            ["git", "-C", str(path), "rev-parse", "--path-format=absolute", "--git-path", "shallow"],
            text=True).strip())
        if shallow_path.is_file():
            # Git bundles omit shallow boundaries. Without these a subsequent
            # --no-local clone tries to traverse parents the bundle lacks.
            shallow_name = "dependencies/" + name + ".shallow"
            copy_file(shallow_path, output / shallow_name)
            repositories[name]["shallow"] = shallow_name
    copy_file(args.musl, output / "dependencies/musl-1.2.5.tar.gz")
    for name, source in (("game", args.game_candidate), ("probe", args.probe_candidate)):
        for file in sorted(source.rglob("*")):
            if file.is_file():
                copy_file(file, output / "candidate-v1" / name / file.relative_to(source))
    (output / "candidate-v1/README.txt").write_text(
        "旧版可测试候选，未经过 Pocket 真机验收。内含匹配的原 SDK runtime 与 ELF。\n"
        "默认 Native/CRT，绝不是此次灰阶 RTL 的新 bitstream；不得混装新 os.bin。\n"
        "game 需要用户合法游戏数据打成 pal.pak；probe 只含合成测试信号。\n"
        "本工具不写 SD 卡、不刷机。请先阅读 source/pocket/docs/acceptance.md。\n")
    for evidence in args.evidence:
        copy_file(evidence, output / "evidence" / evidence.name)
    files = {p.relative_to(output).as_posix(): sha(p) for p in sorted(output.rglob("*")) if p.is_file()}
    manifest = {
        "schema": 1, "kind": "SOURCE_BUILD_HANDOFF_WITH_SEPARATE_LEGACY_CANDIDATE",
        "source_version": "2026-10-05", "repositories": repositories,
        "mister_pal_baseline": PINS["mister_pal"],
        "omitted_source_files": omitted,
        "files": files,
        "not_included": ["proprietary game assets", "account credentials", "Quartus installation media",
                         "Docker images", "complete sbt/Maven cache", "a newly built grayscale bitstream"],
        "not_verified": ["patched Quartus synthesis/fit/assembly/STA", "Pocket/Dock operation",
                         "OGG real-time CPU budget", "LCD aspect/appearance"],
    }
    (output / "handoff-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    files["handoff-manifest.json"] = sha(output / "handoff-manifest.json")
    (output / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for name, digest in sorted(files.items())))
    # A reproducible container of the inputs: stable member order/timestamp,
    # with regular-file modes preserved (no symlinks or hidden Git config).
    args.zip.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.zip, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(output.rglob("*")):
            if not path.is_file():
                continue
            info = zipfile.ZipInfo(output.name + "/" + path.relative_to(output).as_posix(), (2026, 10, 5, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (path.stat().st_mode & 0xffff) << 16
            archive.writestr(info, path.read_bytes())
    with zipfile.ZipFile(args.zip) as archive:
        bad = archive.testzip()
        if bad:
            raise ValueError(f"ZIP CRC failure: {bad}")
    return {"zip": str(args.zip), "bytes": args.zip.stat().st_size, "sha256": sha(args.zip), "files": len(files) + 1}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("sdk", "core", "vexii", "spinal", "rvls", "musl", "game-candidate", "probe-candidate", "output", "zip"):
        parser.add_argument("--" + option, required=True, type=Path)
    parser.add_argument("--evidence", action="append", type=Path, default=[])
    args = parser.parse_args()
    try:
        print(json.dumps(assemble(args), indent=2))
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Handoff creation failed: {exc}\n")


if __name__ == "__main__":
    main()
