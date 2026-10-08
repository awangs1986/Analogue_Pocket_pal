# NOTICE — Licensing & Redistribution

## Upstream engine (SDLPAL)

This project incorporates [SDLPAL](https://github.com/sdlpal/sdlpal), distributed under the
**GNU General Public License, version 3** (GPL-3.0).

Under GPL-3.0 you **may**:

- Modify the software
- Publish forks / branches / redistributed copies
- Use the software privately or publicly

provided you keep the GPL-3.0 terms (including source availability for distributed binaries).

GPL-3.0 **does not allow** adding a blanket “non-commercial only” restriction on the
combined GPL-covered work (see GPL-3.0 §10: no further restrictions). Therefore this
repository’s **software** remains GPL-3.0 and cannot be re-licensed as CC BY-NC / similar
for the combined engine + MiSTer hybrid core.

## MiSTer hybrid core heritage

FPGA / HPS glue in this tree is adapted from GPL-licensed MiSTer hybrid-core work
(e.g. patterns from MiSTer PICO-8 / related cores). That material also remains under GPL-3.0.

## Original game data (Softstar) — NOT included

This repository does **not** include data files from the original *Chinese Paladin /
仙剑奇侠传* game. Those files are proprietary and copyrighted by **SoftStar Inc. / Softstar**.

Do **not** commit or publish:

- `*.mkf`, map/script/data packs, AVI cutscenes, MIDI/RIX music packs from the retail game
- Converted `ogg/` music ripped from game assets
- Screenshots / captures that reproduce copyrighted game art (kept out of this repo)

Obtain game data legally (e.g. purchase the original game) and place it on the MiSTer
under `/media/fat/games/PAL2/` (or your configured Games path) yourself.

## What you can ship from this repo

- Source code (FPGA RTL, ARM/CMake, patched SDLPAL sources)
- Build scripts / handlers
- Optionally FPGA bitstreams (`.rbf`) built from this source

You must **not** ship Softstar game assets with this project.
