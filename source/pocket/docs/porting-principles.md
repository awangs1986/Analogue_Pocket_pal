# Reference ports and decisions

The user asked to apply the six openfpgaOS porting examples. Their lesson is to
retain engine code and replace platform boundaries, not to infer performance
from another game's name. This target consumes the pinned SDK directly; it does
not copy complete engines or proprietary data from these ports.

- [Wolfenstein](https://github.com/openfpgaOS/Wolfenstein): retain SDL-facing game
  interfaces and bridge missing platform behavior locally. Here SDK SDL surfaces
  remain, while incomplete mode/input/file behavior receives game-local adapters.
- [ScummVM](https://github.com/openfpgaOS/ScummVM): an explicit 2D backend separates
  palette, video, audio, input, files and saves. Here those boundaries are separate
  C modules with asset-free contract tests.
- [Diablo](https://github.com/openfpgaOS/Diablo): RPG data and save lifecycle matter
  as much as drawing. Here bounded seekable assets and five persistent slots are
  tested separately, and SD flush is not confused with closing a libc stream.
- [Doom](https://github.com/openfpgaOS/Doom): audio must continue during frame and
  file waits. Here frame/input/delay paths pump the producer, while file idle
  hooks only consume already-decoded PCM, avoiding recursive disk reads.
- [Quake](https://github.com/openfpgaOS/Quake): use runtime services/capabilities,
  coherent frame handoff, and streamed audio. This candidate uses the software
  paletted renderer and PCM stream; it claims neither GPU rendering nor Vorbis
  decoder hardware acceleration merely because other ports use them.
- [Duke3D](https://github.com/openfpgaOS/Duke3D): preserve the software engine's
  canvas while making Pocket output a separate contract. Here native 320×200
  mode/scaler slot 1 plus 4:3 metadata replaces MiSTer's 224-line padding.

Reproducibility is based on `pins.json` and the SDK's runtime manifest, not six
potentially different current bitstreams. The SDK service table header (`of_services.h`) was byte-compared against runtime source `618a3eb985759a4154115109c2c8036271252888`.
The later Core commit `8318f25066b0b75d2e34081919dfc337ac6abf73` is a research
reference only. Neither these source examples nor successful C builds prove
100 MHz FPGA timing closure or PAL performance on a physical Pocket.
