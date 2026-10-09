#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply the tc6 timing-closure patch series to a core prepared by prepare_runtime.py.

Offline and fail-closed: verifies the prepared-core marker, every patch SHA-256 and
`git apply --check` before touching anything. It never builds, flashes or publishes.

The series modifies tracked core files, so a core treated this way is NOT accepted by
the pb driver's later stages (its prepared-core digest changes). Use a separate core
directory and the core's own Make/Quartus flow; see README.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
SERIES = json.loads((HERE / "series.json").read_text())
MARKER = "PAL_TC6_SERIES.json"


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def git(core, *args):
    return subprocess.run(["git", "-C", str(core), *args], check=True, text=True,
                          capture_output=True).stdout


def apply(core, keep_seed=False):
    core = Path(core).resolve()
    prepared = core / "PAL_DISPLAY_MODES_SOURCE_ONLY.json"
    if not prepared.is_file():
        raise ValueError("Not a prepared core (missing PAL_DISPLAY_MODES_SOURCE_ONLY.json); run prepare_runtime.py first")
    marker = json.loads(prepared.read_text())
    want = SERIES["applies_to"]
    if marker.get("source_commit") != want["core_commit"] or marker.get("patch_sha256") != want["display_patch_sha256"]:
        raise ValueError("Prepared core is not 618a3eb + the pinned display-modes.patch")
    if git(core, "rev-parse", "HEAD").strip() != want["core_commit"]:
        raise ValueError("Core HEAD is not the pinned commit")
    if (core / MARKER).exists():
        raise ValueError("Series already applied here; use a fresh prepared core")
    patches = []
    for entry in SERIES["patches"]:
        path = HERE / entry["file"]
        if sha256(path) != entry["sha256"]:
            raise ValueError(f"Patch SHA-256 mismatch: {entry['file']}")
        patches.append(path)
    for path in patches:  # each patch is checked against the tree left by its predecessors
        git(core, "apply", "--check", str(path))
        git(core, "apply", str(path))
    seed = SERIES["seed"]
    if not keep_seed:
        (core / seed["file"]).write_text(f"{seed['value']}\n")
    record = {"status": SERIES["status"], "core_commit": want["core_commit"],
              "patches": SERIES["patches"], "seed": None if keep_seed else seed["value"],
              "series_json_sha256": sha256(HERE / "series.json")}
    (core / MARKER).write_text(json.dumps(record, indent=2) + "\n")
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", required=True, type=Path, help="Core prepared by pocket/fpga/prepare_runtime.py")
    parser.add_argument("--keep-seed", action="store_true", help="Leave seeds/os25.seed unchanged")
    args = parser.parse_args()
    try:
        print(json.dumps(apply(args.core, args.keep_seed), indent=2))
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        detail = getattr(exc, "stderr", "") or ""
        print(f"ERROR: {exc} {detail}".strip(), file=sys.stderr)
        print("A failed git apply may leave earlier patches applied; discard this core directory.", file=sys.stderr)
        return 1
    print("Applied. No bitstream was built; follow README.md and the timing report before any device use.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
