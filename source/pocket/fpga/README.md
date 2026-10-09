# Pocket Display Modes: source-only runtime overlay

This directory contains a game-local patch for the exact runtime source
`618a3eb985759a4154115109c2c8036271252888`. It does **not** change the pinned
SDK, its runtime files, or the existing PAL package. There is no newly compiled
bitstream here. The optional `profiles/color-v1.video.json` accepts the current
v1 runtime and excludes all grayscale-required modes. Its LCD aspect can differ
from 4:3, so it requires explicit opt-in and physical checks. The other three
profiles include grayscale modes and must not be used with the v1 bitstream.

See [implementation, test results and release gates](../docs/display-modes-followup.md).

## Files

- `display-modes.patch`: APF `00B8` command handling, frame-safe grayscale output,
  one new RTL module, QSF integration and the existing bridge-test fixture update.
- `prepare_runtime.py`: create a separate local source checkout, validate and apply
  the patch. Refuses an existing destination or an output inside the source.
- `source.json`: exact base and patch hash, explicitly source-only.
- `profiles/color-v1.video.json`: an optional 16-entry color-input profile that
  needs no rebuilt FPGA runtime; default configuration is not changed.
- `timing-closure-tc6/`: separate, opt-in `git format-patch` series (4 patches, seed 16)
  applied after `display-modes.patch` for the 100 MHz timing-closed tc6-s16 test build.
  Not wired into `build_handoff.py`; not hardware-verified. See its README and
  [the timing report](../docs/timing-closure-tc6.md).
- `profiles/`: three additional full `video.json` variants include monochrome
  modes and together cover all 22 documented IDs, at most 16 per file. Existing
  scaler-slot ABI is unchanged. `catalog.json` records each runtime requirement.

## Run source simulation

From the PAL repository root, with Icarus Verilog 12.0 installed:

```sh
CORE_ROOT=/path/to/openfpgaCore PAL_REQUIRE_RTL=1 \
  python3 -m unittest pocket.tests.test_display_modes -v
```

The core Git object database must contain the exact base commit. `IVERILOG` and
`VVP` can override the executables; `IVERILOG` accepts flags, for example a
locally extracted Debian package's `-B /path/to/usr/lib/x86_64-linux-gnu/ivl`.
All source patching and compilation during tests is confined to temporary trees.
Missing simulators are reported as skipped unless `PAL_REQUIRE_RTL=1`, which
makes their absence a test failure.

## Prepare a separate build tree

```sh
python3 pocket/fpga/prepare_runtime.py \
  --source-core /path/to/openfpgaCore \
  --output /path/to/openfpgaCore-pal-display
```

This performs a local source clone; it does not download dependencies, run
Quartus, create a runtime manifest, package a core or write an SD card. Build and
hardware checks in the linked document are mandatory before releasing a new
grayscale runtime. The v1 color-input profile does not depend on this build,
but its physical appearance remains unverified.
