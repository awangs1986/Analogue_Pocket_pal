# Acceptance checklist

No row below is a physical-device pass until the evidence is collected on a
Pocket with the exact package hash, firmware version, and data pack hash recorded.
A successful host unit test or ELF link must never be copied into that column.

## Before a device run

1. Run `make -C pocket pins`, `make -C pocket check`, and a clean RISC-V build.
2. Inspect `readelf -h -A pocket/build/app.elf`: ELF32, RISC-V, single-float ABI,
   `rv32imafc`; no dynamic interpreter or unresolved symbols.
3. Confirm `pal.pak` contains only your data, valid indexed archive bounds, correct
   OGG names, and game/config/font files for the chosen edition/language.
4. Record candidate SHA-256, SDK pin, runtime manifest, Pocket firmware, PAL
   edition/language, OGG encoder/bitrate/sample rate, and SD model.

## Device evidence required

- Boot: launch PAL instance, reach title, begin game, load one map and battle.
  Missing/corrupt assets must produce an error rather than a silent blank screen.
- Display: inspect all four canvas edges with a 320×200 synthetic border, a
  one-pixel checkerboard, and a PAL scene. No hidden/padded rows or columns.
  Measure displayed image at 4:3. Verify palette fades, shake, dissolve, and RNG.
- Modes: switch normal ↔ CRT and back repeatedly. Check original aspect,
  scanline alignment, text legibility, and no horizontal pixel doubling. LCD
  and monochrome modes are not supported by the current package.
- Controls: all D-pad directions, diagonals, held movement, rapid menu selection,
  A/B/X/Y/L/R/Start/Select mappings, dock keyboard, disconnect/reconnect. No
  double delivery from both pad events and synthesized keyboard events.
- OGG: native 48 kHz stereo and 44.1/22.05 kHz fixtures; mono duplication,
  stereo separation, seamless repeated loops, stop/track switch/mute/fade.
  Test music + repeated SFX during map loads and battles for at least 30 minutes.
  Record decoder worst time, maximum pump gap, software underruns, hardware
  starvation counts, CPU/frame time and heap high water. A host benchmark is
  not a 100 MHz VexiiRiscv benchmark.
- Saves: save each of 1–5 slots, reload in-session, menu Quit, relaunch and
  compare save state. Repeat after updating only ELF. Test no space/error
  reporting without overwriting the only good save. Do not use an abrupt
  power cut as the normal save protocol. Sleep remains disabled.
- Stability: title/game/menu transitions, repeated loads/saves, long scene play,
  game exit. Track memory and audio diagnostics to reveal leaks or starvation.

## Failure gates

- Do not claim complete port or distribute as stable if boot/gameplay/save
  persistence fail, assets are not available, or audio timing remains unmeasured.
- If measured RISC-V Vorbis decoding cannot meet the audio deadline, profile
  decoder hot paths first. A genuine FPGA Vorbis decoder/offload needs new RTL,
  packet/sample transport, resource sizing, synthesis/place-and-route timing,
  and device audio tests. The existing PCM ring/mixer is not that decoder.
- The bundled upstream bitstream has not been rebuilt or timing-closed here.
  Other ports' 100 MHz timing results do not establish timing closure for PAL.
