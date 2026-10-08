#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a deterministic PALPAK1 archive from the user's own PAL assets.

No game data is included or downloaded. Save files (*.rpg, *.sav) are never
packed. Runtime paths are ASCII, case-insensitive, relative to the source root.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import struct
import tempfile

MAGIC = b"PALPAK1\0"
HEADER = struct.Struct("<8s6I")
ENTRY = struct.Struct("<120sII")
MAX_FILES = 4096
MAX_SIZE = 0x7FFFFFFF
MAX_NAME = 119
ALIGNMENT = 16
SAVE_SUFFIXES = {".rpg", ".sav"}


def canonical_name(name: str) -> str:
    """Strict names for disk records; accepts no ambiguous path components."""
    try:
        raw = name.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError(f"Asset name must be ASCII: {name!r}") from exc
    if not raw or len(raw) > MAX_NAME:
        raise ValueError(f"Asset path must contain 1..{MAX_NAME} bytes: {name!r}")
    if any(b < 32 or b > 126 for b in raw) or ":" in name or "\\" in name:
        raise ValueError(f"Invalid asset path: {name!r}")
    if any(part in ("", ".", "..") for part in name.split("/")):
        raise ValueError(f"Asset path must be relative with no dot segments: {name!r}")
    return name.lower()


def source_files(root: Path, output: Path) -> list[tuple[str, Path, int]]:
    if not root.is_dir() or root.is_symlink():
        raise ValueError(f"Not a real asset directory: {root}")
    files: list[tuple[str, Path, int]] = []
    names: set[str] = set()
    for directory, subdirs, filenames in os.walk(root, followlinks=False):
        for item in subdirs + filenames:
            path = Path(directory, item)
            if path.is_symlink():
                raise ValueError(f"Symbolic links are not allowed: {path}")
        for filename in filenames:
            path = Path(directory, filename)
            if path.absolute() == output.absolute() or path.suffix.lower() in SAVE_SUFFIXES:
                continue
            if not path.is_file():
                raise ValueError(f"Not a regular asset file: {path}")
            name = canonical_name(path.relative_to(root).as_posix())
            if name in names:
                raise ValueError(f"Case-insensitive duplicate asset: {name}")
            size = path.stat().st_size
            if size > MAX_SIZE:
                raise ValueError(f"Asset exceeds 2 GiB limit: {name}")
            names.add(name)
            files.append((name, path, size))
            if len(files) > MAX_FILES:
                raise ValueError(f"Archive supports at most {MAX_FILES} files")
    if not files:
        raise ValueError("Asset directory is empty (save files are excluded)")
    return sorted(files)


def pack_archive(root: Path | str, output: Path | str) -> list[dict]:
    root, output = Path(root).absolute(), Path(output).absolute()
    if output.is_symlink():
        raise ValueError("Output must not be a symbolic link")
    files = source_files(root, output)
    cursor = HEADER.size + len(files) * ENTRY.size
    records = []
    for name, path, size in files:
        cursor = (cursor + ALIGNMENT - 1) & -ALIGNMENT
        if cursor > MAX_SIZE or size > MAX_SIZE - cursor:
            raise ValueError("Archive exceeds the portable 2 GiB seek limit")
        records.append({"name": name, "offset": cursor, "size": size, "path": path})
        cursor += size
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary: str | None = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=output.parent,
                                         prefix=output.name + ".", delete=False) as archive:
            temporary = archive.name
            archive.write(HEADER.pack(MAGIC, 1, len(files), HEADER.size,
                                      len(files) * ENTRY.size, cursor, 0))
            for record in records:
                archive.write(ENTRY.pack(record["name"].encode("ascii"),
                                         record["offset"], record["size"]))
            for record in records:
                archive.write(bytes(record["offset"] - archive.tell()))
                with record["path"].open("rb") as source:
                    remaining = record["size"]
                    while remaining:
                        chunk = source.read(min(remaining, 1024 * 1024))
                        if not chunk:
                            raise ValueError(f"Asset shrank during packing: {record['name']}")
                        archive.write(chunk)
                        remaining -= len(chunk)
                    if source.read(1):
                        raise ValueError(f"Asset grew during packing: {record['name']}")
            if archive.tell() != cursor:
                raise ValueError("Internal archive size mismatch")
        read_archive(temporary)
        os.replace(temporary, output)
        temporary = None
    finally:
        if temporary:
            os.unlink(temporary)
    return [{key: value for key, value in r.items() if key != "path"} for r in records]


def read_archive(path: Path | str) -> list[dict]:
    """Validate the exact same bounds/canonical directory contract as files.c."""
    path = Path(path)
    size = path.stat().st_size
    with path.open("rb") as archive:
        header = archive.read(HEADER.size)
        if len(header) != HEADER.size:
            raise ValueError("Truncated archive header")
        magic, version, count, offset, table_size, total, reserved = HEADER.unpack(header)
        if (magic != MAGIC or version != 1 or not 1 <= count <= MAX_FILES or
                offset != HEADER.size or table_size != count * ENTRY.size or
                total != size or total > MAX_SIZE or total < offset + table_size or reserved):
            raise ValueError("Invalid PALPAK1 header")
        result = []
        previous_name = ""
        previous_end = offset + table_size
        for _ in range(count):
            raw = archive.read(ENTRY.size)
            if len(raw) != ENTRY.size:
                raise ValueError("Truncated archive directory")
            raw_name, start, length = ENTRY.unpack(raw)
            if b"\0" not in raw_name:
                raise ValueError("Unterminated asset name")
            name_bytes, padding = raw_name.split(b"\0", 1)
            try:
                name = name_bytes.decode("ascii")
            except UnicodeDecodeError as exc:
                raise ValueError("Non-ASCII archive name") from exc
            if any(padding) or canonical_name(name) != name or name <= previous_name:
                raise ValueError("Noncanonical, duplicate, or unsorted asset name")
            if start < previous_end or start % ALIGNMENT or start > total or length > total - start:
                raise ValueError(f"Invalid asset bounds: {name}")
            result.append({"name": name, "offset": start, "size": length})
            previous_name, previous_end = name, start + length
        if previous_end != total:
            raise ValueError("Trailing archive bytes")
        return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path, help="directory of legally obtained PAL assets")
    parser.add_argument("output", nargs="?", type=Path, help="output archive (normally pal.pak)")
    parser.add_argument("--verify", metavar="ARCHIVE", type=Path, help="validate an existing archive")
    args = parser.parse_args()
    try:
        if args.verify:
            if args.source or args.output:
                parser.error("--verify cannot be combined with source/output")
            result = read_archive(args.verify)
            print(f"Verified {len(result)} assets in {args.verify}")
        else:
            if args.source is None or args.output is None:
                parser.error("source and output are required unless using --verify")
            result = pack_archive(args.source, args.output)
            print(f"Packed {len(result)} assets into {args.output} ({args.output.stat().st_size} bytes)")
            print("Save files (*.rpg, *.sav) were excluded. Keep pal.pak private.")
    except (OSError, ValueError) as exc:
        parser.exit(1, f"pack_assets: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
