#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Restore large source/build inputs from checked-in parts, offline and fail-closed."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
import zlib


def sha(data):
    return hashlib.sha256(data).hexdigest()


def contained(root, relative):
    parts = PurePosixPath(relative)
    if parts.is_absolute() or not parts.parts or any(p in ('.', '..') for p in parts.parts):
        raise ValueError('Unsafe path: ' + relative)
    target = root.joinpath(*parts.parts)
    if root not in target.resolve().parents or any(p.is_symlink() for p in (target, *target.parents) if p != root):
        raise ValueError('Symlink or escaping path: ' + relative)
    return target


def reconstruct(root, verify_only=False):
    root = Path(root).resolve()
    manifest = json.loads((root / 'input-parts.json').read_text())
    if manifest.get('schema') != 1:
        raise ValueError('Unsupported input-parts schema')
    for entry in manifest['inputs']:
        packed = bytearray()
        for part in entry['parts']:
            b = contained(root, part['path']).read_bytes()
            if len(b) != part['size'] or sha(b) != part['sha256']:
                raise ValueError('Part integrity mismatch: ' + part['path'])
            packed.extend(b)
        if sha(packed) != entry['compressed_sha256']:
            raise ValueError('Compressed input hash mismatch: ' + entry['path'])
        decoder = zlib.decompressobj()
        data = decoder.decompress(packed, entry['size'] + 1)
        if not decoder.eof or decoder.unused_data or len(data) != entry['size'] or sha(data) != entry['sha256']:
            raise ValueError('Reconstructed input mismatch: ' + entry['path'])
        target = contained(root, entry['path'])
        if target.exists():
            if not target.is_file() or sha(target.read_bytes()) != entry['sha256']:
                raise ValueError('Refusing to overwrite changed input: ' + entry['path'])
        elif not verify_only:
            target.parent.mkdir(parents=True, exist_ok=True)
            # Exclusive creation avoids overwriting an intervening user edit.
            with target.open('xb') as stream:
                stream.write(data)
    print('PASS: %d large source/dependency inputs verified%s' %
          (len(manifest['inputs']), '' if verify_only else ' and restored'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    try:
        reconstruct(args.root, args.verify_only)
    except (OSError, ValueError, KeyError, zlib.error) as exc:
        print('ERROR:', exc, file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
