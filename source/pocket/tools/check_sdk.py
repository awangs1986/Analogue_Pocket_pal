#!/usr/bin/env python3
"""Fail closed on SDK or bundled runtime mismatch; never mix runtime sets."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parents[1]
PINS = json.loads((HERE / "pins.json").read_text())

def check_sdk(sdk):
    sdk = Path(sdk).resolve()
    commit = subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip()
    if commit != PINS["sdk_commit"]:
        raise ValueError(f"SDK commit is {commit}; required {PINS['sdk_commit']}")
    changes = subprocess.check_output(["git", "-C", str(sdk), "status", "--porcelain", "--", "src/sdk", "runtime"], text=True)
    if changes.strip():
        raise ValueError("SDK headers/runtime are modified; use a pristine pinned checkout")
    manifest = (sdk / "runtime/MANIFEST").read_text()
    if f"source: {PINS['runtime_manifest_source']}" not in manifest:
        raise ValueError("Runtime source manifest mismatch")
    records = {}
    for line in manifest.splitlines():
        if not line or line.startswith("#"):
            continue
        md5, name = line.split()
        records[name.removeprefix("./")] = md5
    verified = {}
    for name in ("pocket/os25.rbf_r", "pocket/os.bin", "pocket/loader.bin"):
        contents = (sdk / "runtime" / name).read_bytes()
        if name not in records or hashlib.md5(contents).hexdigest() != records[name]:
            raise ValueError(f"Runtime checksum mismatch: {name}")
        verified[name] = hashlib.sha256(contents).hexdigest()
    return verified

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True)
    args = parser.parse_args()
    print(json.dumps(check_sdk(args.sdk), indent=2))
