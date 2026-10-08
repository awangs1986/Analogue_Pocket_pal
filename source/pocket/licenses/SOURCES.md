# Third-party notices

The engine and platform derivative use GPL-3.0; full license is at repository
`LICENSE` and in each packaged candidate. OpenfpgaOS SDK/runtime is Apache-2.0,
with its own NOTICE included by the packager. No proprietary sample soundfonts
or game data are included.

The imported engine's Vorbis vendor string is `Xiph.Org libVorbis 1.3.4`.
Its source headers refer to a BSD-style COPYING file which was absent from
this checkout. The accompanying notices are retrieved from Xiph's official
projects; original per-file copyright headers remain unchanged.

- libvorbis: https://github.com/xiph/vorbis/blob/v1.3.4/COPYING
- libogg: https://github.com/xiph/ogg/blob/v1.3.2/COPYING (BSD notice; the mixed
  vendored tree does not identify an exact libogg release in its headers)
- musl: https://musl.libc.org/releases/musl-1.2.5.tar.gz, COPYRIGHT file.
  SDK documents musl 1.2.5. Exact prebuilt-library origin remains the pinned SDK.
- GCC notices/runtime exception: `copyright-gcc.gz` from the verified Debian
  gcc-riscv64-unknown-elf 14.2.0+19 package, included here as GCC-copyright.txt.
  Upstream reference: https://www.gnu.org/licenses/gcc-exception-3.1.html

Retrieved 2026-10-05. These notices do not assert a completed public-release
license/source-distribution audit. Before public redistribution, assemble
corresponding sources for the exact binary components, as described in README.
