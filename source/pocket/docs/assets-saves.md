# PAL assets and save continuity on Pocket

## Data ownership and preparation

The repository and package contain no PAL game data or commercial music. Obtain
and use your own lawful game files. Do not publish the generated `pal.pak` or
include it in a public source/release upload.

Prepare one directory with the game's resource files at its root. Typical
required files are `abc.mkf`, `ball.mkf`, `data.mkf`, `f.mkf`, `fbp.mkf`,
`fire.mkf`, `gop.mkf`, `map.mkf`, `mgo.mkf`, `pat.mkf`, `rgm.mkf`, `rng.mkf`,
`sss.mkf`, `m.msg`, and `word.dat`. Sound resources vary by game edition:
`voc.mkf` or `sounds.mkf`. Keep the files from one compatible game edition.
Custom messages/fonts/configuration must be included if configured.

Place Ogg/Vorbis music in the `ogg` subdirectory, using the track numbers expected
by the engine, for example `ogg/01.ogg`, `ogg/02.ogg`. These must be actual
Ogg/Vorbis files, not other audio formats renamed `.ogg`. See
[audio.md](audio.md) for decoder/playback constraints. OGG playback is streamed
from the archive; the full music library is never loaded into RAM.

A configuration file, if supplied, is `sdlpal.cfg` at the archive root. Runtime
paths refer to the archive root; keep game/save paths at `./` rather than a PC
absolute path. The platform's supported audio/video settings still take priority.

From the repository root:

```sh
python3 pocket/tools/pack_assets.py /path/to/your/pal-data /path/to/pal.pak
python3 pocket/tools/pack_assets.py --verify /path/to/pal.pak
```

Copy your archive into the locally staged package at
`Assets/sdlpal/common/pal.pak`. The packaging script intentionally does not find,
download, or redistribute game data. Creating or installing an SD package does
not constitute a successful boot or hardware test.

The packer includes regular files recursively except `.rpg`/`.sav` saves and its
own output file. Use a dedicated input directory: unrelated files there will
also be packed. It rejects symlinks, non-ASCII filenames, control characters,
parent traversal, duplicate paths after case folding, more than 4096 files,
paths longer than 119 bytes, and archives exceeding 2 GiB minus one byte.
Packing is deterministic: no host timestamps or source absolute paths are
stored. Output is replaced only after a complete new archive passes validation.
The structural verifier checks bounds and names, not whether the supplied bytes
are a compatible/legal PAL edition or valid media.

## APF slot contract

`pocket/config/data.json` declares the core's slot types; `instance.json` binds
those slots to filenames. `files.c` registers the same short filenames with the
SDK. All SDK-registered names are below its 23-character limit.

| APF ID | Filename | Purpose |
| --- | --- | --- |
| 1 | `os.bin` | openfpgaOS runtime |
| 2 | `pal.ini` | Runtime configuration |
| 3 | `pal.elf` | RISC-V engine |
| 4 | `pal.pak` | Read-only asset archive, including OGG tracks |
| 8 | `PAL.cfg` | Nonvolatile `sdlpal.cfg` settings override |
| 10–14 | `PAL_1.sav` … `PAL_5.sav` | Nonvolatile engine save slots 1–5 |

Only one resource data slot and seven explicit SDK filename registrations are
needed, regardless of how many individual MKF/OGG files the archive contains.
The APF bridge addresses and capacities in `data.json` follow the SDK
nonvolatile slot contract. They are not CPU addresses used directly by the app.
Do not copy these descriptors into an unrelated runtime memory layout.

The app interposes only `fopen` and `access` at link time. Asset opens return real
musl `FILE *` streams implemented with `fopencookie`, so `fread`, `fseek`,
`ftell`, `fgets`, EOF/error flags, and Vorbis callbacks use ordinary libc behavior.
Each asset stream has an independent cursor, backed by a bounded region in a
single open archive. The directory is kept in memory (at most 512 KiB), and
payload bytes are read on demand. Reads and seeks cannot escape the named asset.
The SDK file-idle audio hook must only drain already-decoded audio and must not
recursively read this archive.

## Save behavior and existing saves

The original engine names `1.rpg` through `5.rpg` map one-to-one to
`PAL_1.sav` through `PAL_5.sav`. Payloads remain the engine's existing save format;
there is no conversion or additional archive header. A DOS/Windows or differently
configured engine save is not guaranteed compatible just because its name maps.

`PAL.cfg` is a writable override. If absent/empty, reads fall back to the
archive's `sdlpal.cfg`. If neither exists, the engine uses its defaults. Only the
five numbered saves and settings are writable; resource files are read-only.

Each mutable file is limited to 262143 bytes, one byte below the SDK's 256 KiB
slot capacity. The pinned runtime exposes a zero-length numbered save as a
full-capacity read stream so that games can probe their own magic headers.
SDLPAL's raw save format has no such check. The port therefore reserves and
rejects that full-capacity sentinel, including settings reads, rather than
loading an empty slot as a zero-filled game or hiding the archive configuration.
An actual 256 KiB file is also rejected; ordinary PAL saves are smaller. A slot
with missing/stale size metadata is not automatically recovered from raw memory.
Writes are staged in bounded memory, then written through SDK stdio on close. Before commit,
invalid seeks, overflow, and allocation failures abort the staged write and
preserve the previous backing save. A failed open, write, or close is latched for
the platform's save-result UI; a missing optional resource or nonexistent save
during normal lookup does not produce a false save-write error. Two writers to
the same slot are rejected.

On Pocket, writes require the runtime `OF_HW_SAVE_DT_WORD` feature. Older cores
can apply a save's logical size to the wrong compacted APF table entry; the port
refuses writes rather than risking another file. Use the pinned SDK/runtime
package and validate that its capability is present on the device.

**An in-game save is not proof of an SD-card flush.** In the pinned openfpgaOS
kernel (`618a3eb985759a4154115109c2c8036271252888`), closing a nonvolatile file commits its logical size and memory contents;
Pocket's normal nonvolatile writeback persists them when exiting the core.
There is no supported app-level `fsync` promise here. After saving, use the
Pocket menu's **Quit** action, allow writeback to finish, and only then power
off or remove the card. Do not treat power loss, sleep, reset, or a frozen core
as a safe save/exit path. The pinned runtime's physical-device behavior must be
verified as described below. The commit is not power-loss atomic; an SD or
commit failure can still damage a save, so keep backups.

For migration, first create a disposable save on the device and quit cleanly.
Back up the files the Pocket created, then identify its actual `PAL_N.sav`
location on that SD card. Do not assume a host-side directory from another core.
With the card safely mounted and the core stopped, a compatible existing
`N.rpg` can replace the matching `PAL_N.sav` as raw bytes. Keep the original and
backup until a load-and-relaunch test passes. Never pack saves inside `pal.pak`.

## Archive format: PALPAK1

All integers are unsigned little-endian. The 32-byte header consists of:

- 8-byte magic `PALPAK1\0`
- version `1`, file count, directory offset `32`, directory byte length
- exact total archive byte length and a reserved zero word

Each sorted 128-byte directory record contains a 120-byte, NUL-terminated,
zero-padded lowercase ASCII relative path, then 32-bit data offset and length.
Payload starts are 16-byte aligned. Records are lexicographically sorted,
unique, monotonic and nonoverlapping; payloads cannot overlap the directory or
extend beyond the exact file length. Zero-length files are supported. The final
record ends at archive EOF, without an unaccounted trailing region. The runtime
validates the entire directory before making any asset available. There is no
compression, encryption, per-file checksum, or attempt to replace the PAL MKF
format inside an asset.

## Verification and remaining hardware checks

```sh
python3 -m unittest discover -s pocket/tests -p test_pack_assets.py -v
```

All test data is synthesized in temporary directories. Host tests cover:

deterministic packing; case folding/collisions; traversal/symlink/name rejection;
archive count/size bounds; 17 corrupt-header/directory/payload-bound variants
rejected by both Python and C; independent stream cursors and seek/EOF behavior;
empty assets; rejection of zero-filled 256 KiB save/config sentinel streams;
save read/write/update/append/reopen; zero-filled seek gaps;
overflow protection of an existing save; settings fallback/override; duplicate
writers; and injected backing open/write/close failure reporting.

Before trusting a real playthrough, verify on the actual Pocket and SD card:

1. Load a known compatible lawful game dataset and several distinct OGG tracks.
2. Save two visibly different states in different slots. Confirm same-run loads.
3. Quit through the Pocket menu, relaunch, and confirm both states and slot names.
4. Safely inspect and back up the persisted files; compare payloads/lengths.
5. Repeat while alternating another core to detect accidental cross-core writes.
6. Verify the visible failure message using an intentionally unsuitable test
   package/card setup, with valuable saves backed up first.

Host tests do not establish APF DMA behavior, SD persistence, power-loss safety,
or compatibility of every game edition.

Music uses minimum two-digit decimal track numbers: `ogg/01.ogg`, `ogg/02.ogg`, … `ogg/100.ogg`. The standalone synthetic probe intentionally uses its own `ogg/001.ogg` fixture; do not use that probe filename as the game naming template.
