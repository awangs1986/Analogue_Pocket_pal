#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build pal.pak from your own Steam copy of PAL (app 1546570) plus one Workshop music pack.

Copies only (never modifies or moves Steam files), lowercases names, checks that every
required data file and music track is present, then calls pack_assets.py and verifies
the archive. Refuses to overwrite an existing stage directory or pal.pak.

Editions:
  dos    PAL_DOS folder (default; the original awangs1986.PAL core)
  pal98  PAL98 folder (Win95 data: 1-6.avi intro/ending movies, 16-bit sounds.mkf,
         MSG_cht/chs.txt converted by pal98_text.py, CD tracks ogg/100NN.ogg).
         Use it only with the PAL98 test core (awangs1986.PAL98tc6).

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
PAL98_DATA_FILES = ("abc.mkf", "ball.mkf", "data.mkf", "f.mkf", "fbp.mkf", "fire.mkf",
                    "gop.mkf", "map.mkf", "mgo.mkf", "pat.mkf", "rgm.mkf", "rng.mkf",
                    "sss.mkf", "sounds.mkf")
PAL98_MOVIES = tuple(f"{n}.avi" for n in range(1, 7))
# PAL98 scripts request CD audio 2/4/8/9 (op 0x00A3) and the ending uses more;
# the engine looks for ogg/<10000 + track>.ogg and falls back to the Workshop
# music number given in the same script op when a CD track is absent.
PAL98_CD_TRACKS = tuple(range(2, 14))
CD_TRACK = re.compile(r"^(100\d\d)\.ogg$", re.IGNORECASE)
MUSIC = {
    "arranged": {"workshop_id": "2449282849", "title": "SDLPal Arranged Soundtracks",
                 "tracks": set(range(1, 88))},
    "sc": {"workshop_id": "2433259482", "title": "PAL SC Soundtracks",
           "tracks": set(range(1, 88)) - {29}},
}
STEAM_APP_ID = "1546570"
TRACK = re.compile(r"^0*(\d{1,3})\.ogg$", re.IGNORECASE)


def find_dos_dir(pal_dir: Path, sub: str = "pal_dos") -> Path:
    """Accept either steamapps/common/PAL or its PAL_DOS (or PAL98) subfolder."""
    for child in pal_dir.iterdir() if pal_dir.is_dir() else ():
        if child.is_dir() and child.name.lower() == sub:
            return child
    return pal_dir


def lower_index(folder: Path) -> dict[str, Path]:
    by_lower: dict[str, Path] = {}
    for path in folder.iterdir() if folder.is_dir() else ():
        if path.is_file():
            if path.name.lower() in by_lower:
                raise ValueError(f"Case-insensitive duplicate in {folder}: {path.name}")
            by_lower[path.name.lower()] = path
    return by_lower


def collect_pal98(pal98_dir: Path, variant: str, cd_tracks: bool) -> tuple[dict[str, Path], dict[str, Path]]:
    files = lower_index(pal98_dir)
    need = PAL98_DATA_FILES + PAL98_MOVIES + (f"msg_{variant}.txt", f"word_{variant}.txt")
    missing = [name for name in need if name not in files]
    if missing:
        raise ValueError(f"Missing PAL98 files in {pal98_dir}: {' '.join(missing)} "
                         "(use the PAL98 folder of the Steam install)")
    for movie in PAL98_MOVIES:
        with files[movie].open("rb") as handle:
            head = handle.read(12)
        if head[:4] != b"RIFF" or head[8:12] != b"AVI ":
            raise ValueError(f"Not an AVI file: {files[movie]}")
    data = {name: files[name] for name in PAL98_DATA_FILES + PAL98_MOVIES}
    cds: dict[str, Path] = {}
    if cd_tracks:
        found = {}
        ogg_dir = find_dos_dir(pal98_dir, "ogg")
        for path in sorted(lower_index(ogg_dir).values() if ogg_dir != pal98_dir else ()):
            match = CD_TRACK.match(path.name)
            if match:
                with path.open("rb") as handle:
                    if handle.read(4) != b"OggS":
                        raise ValueError(f"Not an Ogg file: {path}")
                found[int(match.group(1)) - 10000] = path
        missing_cd = [n for n in PAL98_CD_TRACKS if n not in found]
        if missing_cd:
            raise ValueError(f"Missing PAL98 CD tracks ogg/{', '.join(str(10000 + n) for n in missing_cd)}.ogg "
                             "(or pass --no-cd-tracks)")
        cds = {f"ogg/{10000 + n}.ogg": found[n] for n in PAL98_CD_TRACKS}
    text = {"msg": files[f"msg_{variant}.txt"], "word": files[f"word_{variant}.txt"]}
    return {**data, **cds}, text


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


def build(pal_dir: Path, music: str, out_dir: Path, music_dir: Path | None = None,
          edition: str = "dos", text_variant: str = "cht", cd_tracks: bool = True) -> dict:
    if edition == "pal98":
        return build_pal98(pal_dir, music, out_dir, music_dir, text_variant, cd_tracks)
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


def build_pal98(pal_dir: Path, music: str, out_dir: Path, music_dir: Path | None,
                text_variant: str, cd_tracks: bool) -> dict:
    from pal98_text import convert  # local module, no game data inside
    spec = MUSIC[music]
    pal_dir = Path(pal_dir)
    music_dir = Path(music_dir) if music_dir else default_workshop_dir(pal_dir, spec["workshop_id"])
    out_dir = Path(out_dir)
    stage, pak = out_dir / "stage", out_dir / "pal.pak"
    if stage.exists() or pak.exists():
        raise ValueError(f"Refusing to overwrite {stage} or {pak}; choose a new --out")
    pal98_dir = find_dos_dir(pal_dir, "pal98")
    data, text = collect_pal98(pal98_dir, text_variant, cd_tracks)
    tracks = collect_music(music_dir, spec["tracks"])
    (stage / "ogg").mkdir(parents=True)
    for name, source in data.items():
        shutil.copyfile(source, stage / name)
    for number, source in tracks.items():
        shutil.copyfile(source, stage / "ogg" / f"{number:02d}.ogg")
    msg_name = f"pal98_{text_variant}.msg"
    report = convert(text["msg"], text["word"], stage / "sss.mkf", stage / msg_name, text_variant)
    if report["missing_ids"] or report["conflicts"]:
        raise ValueError(f"Text conversion problem: {report}")
    (stage / "sdlpal.cfg").write_text(f"MessageFileName={msg_name}\n", encoding="ascii")
    records = pack_archive(stage, pak)
    verified = read_archive(pak)
    expected = len(data) + len(tracks) + 2
    if len(verified) != len(records) or len(records) != expected:
        raise ValueError("Archive entry count mismatch")
    return {"pal_pak": str(pak), "stage": str(stage), "edition": "pal98", "assets": len(records),
            "tracks": len(tracks), "cd_tracks": sum(1 for k in data if k.startswith("ogg/")),
            "text": f"{msg_name} ({report['blocks']} dialog blocks, {report['messages']} lines, {report['words']} words)",
            "music": f"{music} ({spec['workshop_id']})", "bytes": pak.stat().st_size}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pal-dir", required=True, type=Path,
                        help="Steam install folder steamapps/common/PAL (or its PAL_DOS / PAL98 subfolder)")
    parser.add_argument("--edition", choices=("dos", "pal98"), default="dos",
                        help="dos (default) or pal98 = Win95 data with AVI movies (PAL98 test core only)")
    parser.add_argument("--lang", "--text", dest="text", choices=("chs", "cht"), default=None,
                        help="pal98 only: chs = Simplified Chinese (default for pal98), cht = Traditional")
    parser.add_argument("--no-cd-tracks", action="store_true",
                        help="pal98 only: omit ogg/100NN.ogg; those scenes then use the Workshop track")
    parser.add_argument("--music", choices=sorted(MUSIC), default="arranged",
                        help="Workshop music pack (default: arranged, recommended)")
    parser.add_argument("--music-dir", type=Path,
                        help="Downloaded Workshop folder (default: <steamapps>/workshop/content/1546570/<id>)")
    parser.add_argument("--out", required=True, type=Path, help="New output folder for stage/ and pal.pak")
    args = parser.parse_args()
    if args.text is None:
        args.text = "chs" if args.edition == "pal98" else "cht"
    try:
        result = build(args.pal_dir, args.music, args.out, args.music_dir,
                       args.edition, args.text, not args.no_cd_tracks)
    except (OSError, ValueError) as exc:
        print(f"make_pal_pak_from_steam: {exc}", file=sys.stderr)
        return 1
    for key, value in result.items():
        print(f"{key}: {value}")
    print("Verified. Keep pal.pak private; copy it to Assets/<platform>/common/pal.pak on the SD card.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
