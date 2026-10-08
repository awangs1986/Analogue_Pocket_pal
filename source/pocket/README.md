# SDLPAL for Analogue Pocket: RISC-V development candidate

This is a new, isolated `pocket/` target. The MiSTer CMake target, HPS backend,
and game engine sources remain unchanged. It runs the C game engine on the
openfpgaOS VexiiRiscv `rv32imafc` CPU with the `ilp32f` ABI. It is not a complete
game rewritten as RTL, an ARM build, or a claim of verified Pocket performance.

**Status:** implementation/build/test candidate, not a hardware-validated release.
Use the build report for exactly which tests and artifacts were produced.

## Scope

- Native **320 × 200, 8-bit indexed** game surface, pixel-for-pixel software copy.
  No 320 × 224/240 padding, software scaling, or cropped rows. The Pocket scaler
  declares **4:3** display. Incorrect runtime mode negotiation is an error.
- Gamepad and dock keyboard adapted to PAL actions with repeat and edge handling.
- Actual Ogg/Vorbis decoding on RISC-V into a bounded PCM ring; 48 kHz stereo
  PCM playback through openfpgaOS/FPGA audio. WAV/VOC effects retain the engine
  sound player. This is **not FPGA Vorbis decoding**. No predecoded-PCM music
  requirement is substituted for OGG.
- One bounded, indexed `pal.pak` resource archive; seekable stdio preserves engine
  file access without putting every OGG/MKF asset in a separate APF slot.
- Five engine save slots backed by Pocket nonvolatile slots. Software commit
  and physical SD flush are distinct; **exit with the Pocket menu Quit** before
  power-off. This candidate disables sleep.
- Normal display and the built-in CRT mode are declared. Color LCD modes with
  incompatible integer scaling and monochrome modes requiring grayscale are
  not advertised. Declared mode support still needs physical-device testing.
- DOS indexed/RNG cinematics remain in the engine. **Windows AVI playback is
  disabled** in this first indexed candidate. MIDI/RIX/MP3/Opus music is not
  substituted for the requested OGG audio.

## Build

Requirements: GNU Make, Python 3, host C compiler for tests, and an official
RISC-V GCC/binutils toolchain with `rv32imafc/ilp32f` multilib. The tested compiler
is Debian `gcc-riscv64-unknown-elf 14.2.0+19` with binutils `2.44-3+7+b1`.
Package SHA-256 values are in `pins.json`. The SDK's pinned musl is linked
explicitly; do not substitute host libc/newlib or a RISC-V Linux glibc toolchain.

From the repository root, with a sibling SDK checkout:

```sh
git clone https://github.com/openfpgaOS/openfpgaSDK.git ../openfpgaSDK
git -C ../openfpgaSDK checkout a408ddc12aed0dfaa4aa22c06af82f829db77126
make -C pocket pins
make -C pocket CROSS=riscv64-unknown-elf- -j4
make -C pocket check
make -C pocket package CROSS=riscv64-unknown-elf-
```

Use `SDK_ROOT=/absolute/path/to/openfpgaSDK` and `CROSS=/path/bin/riscv64-unknown-elf-`
when needed. This external-SDK target explicitly uses the host-toolchain path,
`USE_SDK_CONTAINER=0`; it does not claim the upstream SDK container was used.
Clean the object directory before changing compiler/toolchain. Outputs are
`pocket/build/app.elf`, `pal.map`, and `package.zip`. Packaging refuses an
existing output directory or mismatched SDK/runtime instead of overwriting or
mixing them; use a new `--out` path for a second candidate.

The SDK commit and its bundled runtime manifest are pinned as a set. The
manifest's runtime source is `618a3eb`, distinct from the newer Core source
reference examined during research. Do not swap in a bitstream built from
that newer checkout and assume compatibility.

## Game data and installation

No copyrighted PAL game files, music, or SDK sample soundfonts are bundled.
Supply your lawfully held game data and OGG tracks. See
[assets and saves](docs/assets-saves.md) for pack creation and filenames.
The resulting `pal.pak` belongs at `Assets/sdlpal/common/pal.pak` in the staged
package. Nothing in this task copies the package to an SD card or flashes hardware.

The package is a local development candidate. No publication or public release
is performed. Before redistribution, assemble corresponding sources and notices
for the exact engine, SDK/runtime, musl, Vorbis, and compiler runtime used; this
candidate is not a substitute for that release audit.

## Validation

See [acceptance checklist](docs/acceptance.md), [audio limits](docs/audio.md),
[display-mode coverage](docs/display-and-input.md), and the generated build report.
The [copyright-free hardware probe](docs/probe.md) can be used before supplying game data.
Host tests and successful cross-linking are not Pocket boot/gameplay/performance,
Quartus timing closure, physical save persistence, or CRT visual validation.
