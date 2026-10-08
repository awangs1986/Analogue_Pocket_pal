#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject incomplete boot MIFs, including upstream's missing-hexdump false pass."""
import argparse
from pathlib import Path
import re
import struct


def verify_boot_image(mif_path, boot_path):
    text = Path(mif_path).read_text()
    boot = Path(boot_path).read_bytes()
    depth_match = re.search(r"(?m)^DEPTH=(\d+);$", text)
    if "WIDTH=32;" not in text or not depth_match or not boot or len(boot) % 4:
        raise ValueError("Expected nonempty word-aligned boot.bin and 32-bit MIF")
    depth = int(depth_match[1])
    if depth <= 0 or depth > 262144 or len(boot) > depth * 4:
        raise ValueError("Invalid BRAM depth or oversized boot payload")
    words = {}
    for line in text.splitlines():
        if match := re.fullmatch(r"(\d+) : ([0-9A-Fa-f]{8});", line):
            indices, value = [int(match[1])], int(match[2], 16)
        elif match := re.fullmatch(r"\[(\d+)\.\.(\d+)\] : ([0-9A-Fa-f]{8});", line):
            start, end = int(match[1]), int(match[2])
            if start > end or end >= depth:
                raise ValueError("Invalid MIF fill range")
            indices, value = range(start, end + 1), int(match[3], 16)
        else:
            continue
        for index in indices:
            if index < 0 or index >= depth or index in words:
                raise ValueError("Duplicate or out-of-bounds MIF address")
            words[index] = value
    if len(words) != depth:
        raise ValueError("Incomplete MIF address coverage")
    rendered = b"".join(struct.pack("<I", words[i]) for i in range(depth))
    if rendered[:len(boot)] != boot:
        raise ValueError("MIF does not match boot.bin; check hexdump/build failure")
    if any(words[i] != 0x13 for i in range(len(boot) // 4, depth)):
        raise ValueError("Unexpected BRAM padding (expected RISC-V NOP)")
    return {"boot_bytes": len(boot), "boot_words": len(boot) // 4, "bram_words": depth}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mif", type=Path)
    parser.add_argument("boot", type=Path)
    args = parser.parse_args()
    print(verify_boot_image(args.mif, args.boot))
