#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage an asset-free, isolated TEST SD tree (+ zip) for the tc6-s16 bitstream.

Layout mirrors package.py, but uses a separate core ID (awangs1986.PALtc6) and platform ID
(sdlpaltc6) so it can never overwrite an awangs1986.PAL install or its saves. Inputs are
read-only and hash-pinned (see ../fpga/timing-closure-tc6/series.json); outputs refuse to
overwrite. No game data is packaged: the user supplies pal.pak. Never flashes anything.

Example (paths are illustrative):
  python3 package_tc6.py --tc-core ~/fpga/tc/core --out ~/fpga/release/tc6-s16-sd
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile

from package import CORE_JSON_FILES, verify_elf

HERE = Path(__file__).resolve().parent          # source/pocket/tools
POCKET = HERE.parent                             # source/pocket
REPO = POCKET.parents[1]
SERIES = json.loads((POCKET / "fpga/timing-closure-tc6/series.json").read_text())
AUTHOR, CORE_SHORT, PLATFORM = "awangs1986", "PALtc6", "sdlpaltc6"
CORE_ID = f"{AUTHOR}.{CORE_SHORT}"
EXPECT = {"rbf_r": SERIES["reference_result_tc6_s16"]["os25.rbf_r_sha256"],
          "mif": SERIES["reference_result_tc6_s16"]["embedded_firmware_mif_sha256"],
          "os": SERIES["reference_result_tc6_s16"]["os.bin_sha256_same_firmware_build"],
          "elf": "dbaea4bddad859a240685a25e9648944fa6b6ec2664f1108defb8893143aab9e"}
BITREV = bytes(int(f"{x:08b}"[::-1], 2) for x in range(256))
INFO_TXT = (
    "SDLPAL (PAL) - TEST BUILD tc6-s16, NOT FOR GENERAL USE\n"
    "Bitstream: openfpgaCore 618a3eb + display-modes patch + timing-closure-tc6 series (7c29497), seed 16.\n"
    "Timing: all 4 corners positive (min hold +0.009 ns). NOT verified on hardware.\n"
    "GPU is a register-compatible stub (EXCLUDE_GPU): only for PAL; GPU-drawing apps will not render.\n"
    "build_id is 0. No game data included: put your own pal.pak in Assets/sdlpaltc6/common/.\n"
    "Saves: use Pocket menu Quit before power-off. Sleep not supported.\n"
    "Remove: delete Cores/awangs1986.PALtc6, Platforms/sdlpaltc6.json, Assets/sdlpaltc6 (Saves/sdlpaltc6 if wanted).\n")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def check(cond, message):
    if not cond:
        raise ValueError(message)


def stage(args):
    work = args.work.resolve()
    sdk = (args.sdk or work / "deps/openfpgaSDK").resolve()
    tc_core = args.tc_core.resolve()
    bits = (args.bitstream_dir or tc_core / "src/fpga/targets/pocket/bld/tc6-s16/output_files").resolve()
    os_bin = (args.os_bin or work / "build/core/src/firmware/os/bld/pocket/os.bin").resolve()
    elf = (args.elf or work / "build/pal/pocket/build/game/app.elf").resolve()
    out = args.out.resolve()
    zip_path = (args.zip or out.with_suffix(".zip")).resolve()
    check(not out.exists() and not zip_path.exists(), f"Output exists: {out} or {zip_path}; choose a new name")

    rbf_r, rbf = bits / "os25.rbf_r", bits / "ap_core.rbf"
    mif = tc_core / "src/fpga/targets/pocket/firmware.mif"
    pinned = not args.unpinned
    if pinned:
        check(sha(rbf_r) == EXPECT["rbf_r"], "os25.rbf_r is not the tc6-s16 bitstream (use --unpinned for another build)")
        check(sha(mif) == EXPECT["mif"], "firmware.mif in --tc-core differs from the one embedded in tc6-s16")
        check(sha(os_bin) == EXPECT["os"], "os.bin is not from the same firmware build as the tc6-s16 MIF")
        check(sha(elf) == EXPECT["elf"], "app.elf hash differs from the tc6-s16 test package")
    check(rbf.read_bytes().translate(BITREV) == rbf_r.read_bytes(), "os25.rbf_r != bit-reversed ap_core.rbf")
    verify_elf(elf)
    sys.path.insert(0, str(HERE))
    from check_sdk import check_sdk
    sdk_hashes = check_sdk(sdk)  # pinned SDK commit, clean tree, runtime MANIFEST
    loader = sdk / "runtime/pocket/loader.bin"
    asm = "src/fpga/targets/pocket/chip32/loader.asm"
    pinned_core = work / "deps/openfpgaCore"
    if (pinned_core / asm).is_file():
        check((pinned_core / asm).read_bytes() == (tc_core / asm).read_bytes(), "loader.asm differs between pinned core and --tc-core")

    core = out / "Cores" / CORE_ID
    common = out / "Assets" / PLATFORM / "common"
    inst = out / "Assets" / PLATFORM / CORE_ID
    plats = out / "Platforms"
    info = out / "PALtc6-test-info"
    for d in (core, common, inst, plats, info / "licenses", info / "optional-video-profiles"):
        d.mkdir(parents=True)
    for name in CORE_JSON_FILES:  # includes variants.json (required by the Pocket firmware)
        value = json.loads((POCKET / "config" / name).read_text())
        if name == "core.json":
            md = value["core"]["metadata"]
            md.update(platform_ids=[PLATFORM], shortname=CORE_SHORT,
                      description="TEST tc6-s16 7c29497: NOT HW-verified, GPU stub",
                      version="0.1.0-test.tc6-s16.7c29497", date_release="2026-10-09")
            check(len(md["description"]) <= 63 and len(md["version"]) <= 31, "core.json metadata too long")
        (core / name).write_text(json.dumps(value, indent=2) + "\n")
    platform = json.loads((POCKET / "config/platform.json").read_text())
    platform["platform"]["name"] = "SDLPAL TEST tc6-s16"
    (plats / f"{PLATFORM}.json").write_text(json.dumps(platform, indent=2) + "\n")
    shutil.copyfile(POCKET / "config/instance.json", inst / "PAL.json")
    shutil.copyfile(POCKET / "config/pal.ini", common / "pal.ini")
    shutil.copyfile(elf, common / "pal.elf")
    shutil.copyfile(os_bin, common / "os.bin")
    shutil.copyfile(rbf_r, core / "os25.rbf_r")
    shutil.copyfile(loader, core / "loader.bin")
    (core / "info.txt").write_text(INFO_TXT)
    for name in ("generic.video.json", "catalog.json"):
        shutil.copyfile(POCKET / "fpga/profiles" / name, info / "optional-video-profiles" / name)
    lic = info / "licenses"
    for src, dst in ((REPO / "source/LICENSE", "SDLPAL-GPL-3.0.txt"), (REPO / "source/NOTICE.md", "SDLPAL-NOTICE.md"),
                     (sdk / "LICENSE", "openfpgaOS-SDK-Apache-2.0.txt"), (sdk / "NOTICE", "openfpgaOS-SDK-NOTICE.txt"),
                     (sdk / "LICENSES/MIT.txt", "musl-MIT.txt"), (tc_core / "LICENSE", "openfpgaCore-LICENSE.txt"),
                     (tc_core / "NOTICE", "openfpgaCore-NOTICE.txt")):
        shutil.copyfile(src, lic / dst)
    shutil.copytree(POCKET / "licenses", lic / "additional")
    shutil.copytree(POCKET / "docs", info / "docs")
    shutil.copyfile(POCKET / "pins.json", info / "pins.json")
    shutil.copyfile(POCKET / "README.md", info / "PAL-port-README.md")
    prov = {
        "package": "tc6-s16 isolated TEST candidate (not a release; not hardware-verified)",
        "core_id": CORE_ID, "platform_id": PLATFORM, "hash_pinned": pinned,
        "bitstream": {"file": f"Cores/{CORE_ID}/os25.rbf_r", "sha256": sha(rbf_r),
                      "ap_core.rbf_sha256": sha(rbf), "series": "source/pocket/fpga/timing-closure-tc6",
                      "origin_commit": SERIES["origin_commits"]["head"], "seed": SERIES["seed"]["value"],
                      "macros": " ".join(SERIES["build"]["macros"]), "quartus": SERIES["build"]["quartus"],
                      "device": SERIES["build"]["device"], "embedded_firmware_mif_sha256": sha(mif)},
        "os.bin": {"sha256": sha(os_bin), "note": "same firmware build as the MIF inside the bitstream"},
        "loader.bin": {"sha256": sha(loader), "source": "pinned SDK runtime/pocket/loader.bin (MANIFEST verified)"},
        "pal.elf": {"sha256": sha(elf)},
        "pinned_sdk_runtime_checked": sdk_hashes,
        "not_included": ["pal.pak (user-supplied game data)", "any commercial game data/music/soundfont"],
    }
    (info / "RUNTIME-PROVENANCE.json").write_text(json.dumps(prov, indent=2) + "\n")
    files = [p for p in sorted(out.rglob("*")) if p.is_file()]
    (out / "SHA256SUMS").write_text("".join(f"{sha(p)}  {p.relative_to(out)}\n" for p in files))
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(out.rglob("*")):
            if path.is_file():
                item = zipfile.ZipInfo(str(path.relative_to(out)), (2026, 10, 9, 0, 0, 0))
                item.external_attr = 0o100644 << 16
                item.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(item, path.read_bytes())
    return {"stage": str(out), "zip": str(zip_path), "zip_sha256": sha(zip_path), "rbf_r_sha256": sha(rbf_r)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tc-core", required=True, type=Path, help="Core tree with the tc6 series applied and built")
    parser.add_argument("--out", required=True, type=Path, help="New staging directory (must not exist)")
    parser.add_argument("--zip", type=Path, help="Zip path (default: <out>.zip)")
    parser.add_argument("--work", type=Path, default=REPO / "work", help="Project work dir (default: <repo>/work)")
    parser.add_argument("--sdk", type=Path, help="Pinned openfpgaSDK (default: <work>/deps/openfpgaSDK)")
    parser.add_argument("--bitstream-dir", type=Path, help="Quartus output_files dir (default: <tc-core>/.../bld/tc6-s16/output_files)")
    parser.add_argument("--os-bin", type=Path, help="os.bin from the firmware build embedded in the bitstream")
    parser.add_argument("--elf", type=Path, help="PAL app.elf (default: <work>/build/pal/pocket/build/game/app.elf)")
    parser.add_argument("--unpinned", action="store_true", help="Skip tc6-s16 hash pins (records hash_pinned=false)")
    args = parser.parse_args()
    try:
        print(json.dumps(stage(args), indent=2))
    except (ValueError, OSError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
