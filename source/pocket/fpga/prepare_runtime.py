#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create an isolated, source-only runtime checkout; never edit the pinned SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

BASE_COMMIT = "618a3eb985759a4154115109c2c8036271252888"
HERE = Path(__file__).resolve().parent
PATCH = HERE / "display-modes.patch"
PATCH_PATHS = (
    "src/fpga/targets/pocket/ap_core.qsf",
    "src/fpga/targets/pocket/core_bridge_cmd.v",
    "src/fpga/targets/pocket/core_top.v",
    "src/fpga/targets/pocket/pal_display_mode_video.v",
    "src/fpga/test/tb_core_bridge_cmd.v",
)


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def prepare(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    if output.exists():
        raise ValueError("Output must not exist; use a new game-local build directory")
    if source == output or source in output.parents:
        raise ValueError("Output must be outside the source checkout")
    # Local clone only: no download, toolchain execution, submodule update,
    # firmware/bitstream build, installation, SD write, or publication.
    run("git", "-C", str(source), "cat-file", "-e", BASE_COMMIT + "^{commit}")
    output.parent.mkdir(parents=True, exist_ok=True)
    run("git", "clone", "--no-local", "--no-checkout", str(source), str(output))
    run("git", "-C", str(output), "checkout", "--detach", BASE_COMMIT)
    run("git", "-C", str(output), "apply", "--check", str(PATCH))
    run("git", "-C", str(output), "apply", str(PATCH))
    # Do not produce a runtime MANIFEST: that would imply a coherent binary
    # build. This marker explicitly identifies a SOURCE-ONLY overlay.
    provenance = {
        "status": "SOURCE_ONLY_NOT_A_RUNTIME",
        "source_commit": BASE_COMMIT,
        "patch_sha256": hashlib.sha256(PATCH.read_bytes()).hexdigest(),
        "modified_paths": list(PATCH_PATHS),
        "requires": ["submodule materialization", "pinned Quartus full build",
                     "timing and resource reports", "Pocket and Dock acceptance"],
    }
    (output / "PAL_DISPLAY_MODES_SOURCE_ONLY.json").write_text(
        json.dumps(provenance, indent=2) + "\n")
    return provenance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-core", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        provenance = prepare(args.source_core, args.output)
    except (ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Source preparation failed: {exc}\n")
    print(json.dumps(provenance, indent=2))
    print("No new bitstream exists. Do not install monochrome profiles on the v1 runtime.")


if __name__ == "__main__":
    main()
