#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build pal.pak from your own Steam copy of PAL (app 1546570) plus one Workshop music pack.

Copies only (never modifies or moves Steam files), lowercases names, checks that every
required DOS data file and music track is present, then calls pack_assets.py and verifies
the archive. Refuses to overwrite an existing stage directory or pal.pak.

Music packs (subscribe in the Steam Workshop first):
  arranged  2449282849 "SDLPal Arranged Soundtracks"  tracks 01-87   (recommended)
  sc        2433259482 "PAL SC Soundtracks"           86 tracks, no 29

No game data is included in this repository or downloaded by this tool.
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pack_assets import pack_archive, read_archive  # noqa: E402

DATA_FILES = ("abc.mkf", "ball.mkf", "data.mkf", "f.mkf", "fbp.mkf", "fire.mkf", "gop.mkf",
              "map.mkf", "mgo.mkf", "pat.mkf", "rgm.mkf", "rng.mkf", "sss.mkf",
              "voc.mkf", "m.msg", "word.dat", "wor16.asc", "wor16.fon")
MUSIC = {
    "arranged": {"workshop_id": "2449282849", "title": "SDLPal Arranged Soundtracks",
                 "tracks": set(range(1, 88))},
    "sc": {"workshop_id": "2433259482", "title": "PAL SC Soundtracks",
           "tracks": set(range(1, 88)) - {29}},
}
STEAM_APP_ID = "1546570"
TRACK = re.compile(r"^0*(\d{1,3})\.ogg$", re.IGNORECASE)


def find_dos_dir(pal_dir: Path) -> Path:
    """Accept either steamapps/common/PAL or its PAL_DOS subfolder."""
    for child in pal_dir.iterdir() if pal_dir.is_dir() else ():
        if child.is_dir() and child.name.lower() == "pal_dos":
            return child
    return pal_dir


def default_workshop_dir(pal_dir: Path, workshop_id: str) -> Path:
    # <library>/steamapps/common/PAL -> <library>/steamapps/workshop/content/1546570/<id>
    steamapps = pal_dir.resolve().parent.parent
    return steamapps / "workshop" / "content" / STEAM_APP_ID / workshop_id


def collect_data(dos_dir: Path) -> dict[str, Path]:
    by_lower: dict[str, Path] = {}
    for path in dos_dir.iterdir():
        if path.is_file():
            if path.name.lower() in by_lower:
                raise ValueError(f"Case-insensitive duplicate in {dos_dir}: {path.name}")
            by_lower[path.name.lower()] = path
    missing = [name for name in DATA_FILES if name not in by_lower]
    if missing:
        raise ValueError(f"Missing DOS data files in {dos_dir}: {' '.join(missing)} "
                         "(use the PAL_DOS folder of the Steam install)")
    return {name: by_lower[name] for name in DATA_FILES}


def collect_music(music_dir: Path, expected: set[int]) -> dict[int, Path]:
    if not music_dir.is_dir():
        raise ValueError(f"Music folder not found: {music_dir} (subscribe in the Workshop and let Steam download it)")
    tracks: dict[int, Path] = {}
    for path in sorted(music_dir.rglob("*")):
        match = TRACK.match(path.name)
        if not match or not path.is_file():
            continue
        number = int(match.group(1))
        if number in tracks:
            raise ValueError(f"Duplicate track {number:02d}: {tracks[number]} and {path}")
        with path.open("rb") as handle:
            if handle.read(4) != b"OggS":
                raise ValueError(f"Not an Ogg file: {path}")
        tracks[number] = path
    found = set(tracks)
    if found != expected:
        missing = sorted(expected - found)
        extra = sorted(found - expected)
        raise ValueError(f"Unexpected track set in {music_dir}: missing {missing} extra {extra}")
    return tracks


def build(pal_dir: Path, music: str, out_dir: Path, music_dir: Path | None = None) -> dict:
    spec = MUSIC[music]
    pal_dir = Path(pal_dir)
    music_dir = Path(music_dir) if music_dir else default_workshop_dir(pal_dir, spec["workshop_id"])
    out_dir = Path(out_dir)
    stage, pak = out_dir / "stage", out_dir / "pal.pak"
    if stage.exists() or pak.exists():
        raise ValueError(f"Refusing to overwrite {stage} or {pak}; choose a new --out")
    data = collect_data(find_dos_dir(pal_dir))
    tracks = collect_music(music_dir, spec["tracks"])
    (stage / "ogg").mkdir(parents=True)
    for name, source in data.items():
        shutil.copyfile(source, stage / name)
    for number, source in tracks.items():
        shutil.copyfile(source, stage / "ogg" / f"{number:02d}.ogg")
    records = pack_archive(stage, pak)
    verified = read_archive(pak)
    if len(verified) != len(records) or len(records) != len(DATA_FILES) + len(tracks):
        raise ValueError("Archive entry count mismatch")
    return {"pal_pak": str(pak), "stage": str(stage), "assets": len(records),
            "tracks": len(tracks), "music": f"{music} ({spec['workshop_id']})",
            "bytes": pak.stat().st_size}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pal-dir", required=True, type=Path,
                        help="Steam install folder steamapps/common/PAL (or its PAL_DOS subfolder)")
    parser.add_argument("--music", choices=sorted(MUSIC), default="arranged",
                        help="Workshop music pack (default: arranged, recommended)")
    parser.add_argument("--music-dir", type=Path,
                        help="Downloaded Workshop folder (default: <steamapps>/workshop/content/1546570/<id>)")
    parser.add_argument("--out", required=True, type=Path, help="New output folder for stage/ and pal.pak")
    args = parser.parse_args()
    try:
        result = build(args.pal_dir, args.music, args.out, args.music_dir)
    except (OSError, ValueError) as exc:
        print(f"make_pal_pak_from_steam: {exc}", file=sys.stderr)
        return 1
    for key, value in result.items():
        print(f"{key}: {value}")
    print("Verified. Keep pal.pak private; copy it to Assets/<platform>/common/pal.pak on the SD card.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
