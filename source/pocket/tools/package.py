#!/usr/bin/env python3
"""Assemble a local, asset-free Pocket candidate from a verified runtime set."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile
from check_sdk import check_sdk, HERE, PINS

CORE_ID = "awangs1986.PAL"

def verify_elf(path):
    head = Path(path).read_bytes()[:52]
    if len(head) < 52 or head[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Expected 32-bit little-endian ELF")
    machine = struct.unpack_from("<H", head, 18)[0]
    flags = struct.unpack_from("<I", head, 36)[0]
    if machine != 243 or flags & 6 != 2:
        raise ValueError("Expected RISC-V ELF with single-float ABI")


def package(sdk, elf, out, probe_assets=None):
    sdk, elf, out = Path(sdk).resolve(), Path(elf).resolve(), Path(out).resolve()
    runtime_hashes = check_sdk(sdk)
    verify_elf(elf)
    if out.exists():
        raise ValueError(f"Output exists: {out}; choose a new output directory")
    platform_id = "sdlpalprobe" if probe_assets else "sdlpal"
    core_id = "awangs1986.PALProbe" if probe_assets else CORE_ID
    core = out / "Cores" / core_id
    common = out / "Assets" / platform_id / "common"
    instances = out / "Assets" / platform_id / core_id
    platforms = out / "Platforms"
    licenses = out / "licenses"
    for directory in (core, common, instances, platforms, licenses):
        directory.mkdir(parents=True)
    for name in ("core.json", "audio.json", "data.json", "input.json", "video.json", "interact.json"):
        value = json.loads((HERE / "config" / name).read_text())
        if probe_assets and name == "core.json":
            value["core"]["metadata"]["platform_ids"] = [platform_id]
            value["core"]["metadata"]["shortname"] = "PAL Probe"
            value["core"]["metadata"]["description"] = "Synthetic RISC-V video/OGG probe; no save writes"
        (core / name).write_text(json.dumps(value, indent=2) + "\n")
    platform = json.loads((HERE / "config/platform.json").read_text())
    if probe_assets:
        platform["platform"].update(name="PAL Synthetic Probe", year=2026, manufacturer="SDLPAL contributors")
    (platforms / (platform_id + ".json")).write_text(json.dumps(platform, indent=2) + "\n")
    shutil.copyfile(HERE / "config/instance.json", instances / "PAL.json")
    shutil.copyfile(HERE / "config/pal.ini", common / "pal.ini")
    shutil.copyfile(elf, common / "pal.elf")
    if probe_assets:
        from pack_assets import read_archive
        read_archive(Path(probe_assets))
        shutil.copyfile(probe_assets, common / "pal.pak")
    for name in ("os25.rbf_r", "loader.bin"):
        shutil.copyfile(sdk / "runtime/pocket" / name, core / name)
    shutil.copyfile(sdk / "runtime/pocket/os.bin", common / "os.bin")
    shutil.copyfile(sdk / "runtime/MANIFEST", out / "runtime-MANIFEST.txt")
    shutil.copyfile(HERE.parent / "LICENSE", licenses / "SDLPAL-GPL-3.0.txt")
    shutil.copyfile(HERE.parent / "NOTICE.md", licenses / "SDLPAL-NOTICE.md")
    shutil.copyfile(sdk / "LICENSE", licenses / "openfpgaOS-Apache-2.0.txt")
    shutil.copyfile(sdk / "NOTICE", licenses / "openfpgaOS-NOTICE.txt")
    shutil.copyfile(sdk / "LICENSES/MIT.txt", licenses / "musl-MIT.txt")
    shutil.copytree(HERE / "licenses", licenses / "additional")
    shutil.copyfile(HERE / "pins.json", out / "pins.json")
    shutil.copyfile(HERE / "README.md", out / "README.md")
    shutil.copytree(HERE / "docs", out / "docs")
    if probe_assets:
        (out / "PROBE_INFO.txt").write_text(
            "Synthetic OGG and display-pattern probe. No copyrighted game assets required.\n"
            "Uses separate core/asset IDs; does not write saves or settings.\n"
            "Press Start for CPU/decode/underrun status; see docs/probe.md.\n"
            "This binary was cross-built but has NOT been run on a physical Pocket.\n")
    else:
      (out / "ASSETS_REQUIRED.txt").write_text(
        "Development candidate. Not yet tested on a physical Pocket.\n"
        "No proprietary game data, OGG songs, or sample soundfonts are included.\n"
        "Create pal.pak from your lawfully held game data using pocket/tools/pack_assets.py,\n"
        "then put it in Assets/sdlpal/common/. See README.md and docs/assets-saves.md.\n"
        "Menu Quit is required to flush saves. Sleep is disabled. Do not power off immediately after saving.\n")
    manifest = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(out.rglob("*")) if p.is_file()}
    (out / "SHA256SUMS").write_text("".join(f"{h}  {n}\n" for n, h in manifest.items()))
    zip_path = out.with_suffix(".zip")
    if zip_path.exists():
        raise ValueError(f"ZIP output exists: {zip_path}")
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(out.rglob("*")):
            if path.is_file():
                info = zipfile.ZipInfo(str(path.relative_to(out)), (2026, 10, 5, 0, 0, 0))
                info.external_attr = 0o100644 << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, path.read_bytes())
    return {"package": str(zip_path), "sha256": hashlib.sha256(zip_path.read_bytes()).hexdigest(),
            "runtime": runtime_hashes, "sdk": PINS["sdk_commit"]}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--probe-assets", help="Synthetic pal.pak for an isolated PALProbe package")
    args = parser.parse_args()
    print(json.dumps(package(args.sdk, args.elf, args.out, args.probe_assets), indent=2))
