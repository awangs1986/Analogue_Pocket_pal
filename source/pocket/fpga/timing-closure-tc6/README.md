# timing-closure-tc6: openfpgaCore timing-closure patch series (TEST, not hardware-verified)

Four `git format-patch` patches that take the prepared core
(openfpgaCore `618a3eb` + `../display-modes.patch`) to the tree that produced the
**tc6-s16** bitstream, the only build in the sweep with all four timing corners
positive at 100 MHz. Full Chinese report: `../../docs/timing-closure-tc6.md`.

| # | Patch | Change |
|---|---|---|
| 0001 | io_sdram registered one-hot DQ selects | `io_sdram.v`: registered one-hot selects for `phy_dq_out` (bit-exact, lockstep-simulated) |
| 0002 | drop PAL-unused GPU features | `variants/os25.mk`: drop PAL-unused GPU features (TRANSLUC, PALETTE, COLUMN_LIST, COMPACT_SPAN, PARAM_SPAN_Q29) |
| 0003 | EXCLUDE_GPU gpu_min stub | new `common/gpu_min.v` register-compatible stub (immediate completion, fence retired, GPU SDRAM masters idle); `ap_core.qsf`, `core_top.v`, `os25.mk` |
| 0004 | false path to unused DDIO rising-edge capture | `core_constraints.sdc` (+16): false path only to the 16 unused DDIO `dataout_h` capture regs; the real capture stays constrained |

`series.json` pins the base, every patch SHA-256, the seed (16), macros and the
tc6-s16 reference hashes/slacks.

## Why it is not wired into `build_handoff.py`

`pb prepare-core` records a digest of the prepared core and every later stage
re-checks it. Applying this series changes tracked files, so the existing pb
`firmware`/`fpga` stages would (correctly) refuse that tree. To keep the existing
validation intact, the series is a separate, opt-in step on a separate core copy.

## Replay

```sh
# 1. prepared core (same as pb prepare-core uses), in a NEW directory
python3 source/pocket/fpga/prepare_runtime.py --source-core work/deps/openfpgaCore --output /path/to/tc6-core
# 2. apply the series (verifies marker + patch hashes, git apply --check, writes seeds/os25.seed = 16)
python3 source/pocket/fpga/timing-closure-tc6/apply_series.py --core /path/to/tc6-core
# 3. materialize submodules and build os25 with the core's own Make/Quartus flow
#    (Quartus 25.1std Lite, 100 MHz targets unchanged). Seed-sensitive: the reference
#    sweep found 1 of 11 seeds all-positive; check all four corners before using any build.
# 4. stage an isolated SD test tree (no game data):
python3 source/pocket/tools/package_tc6.py --tc-core /path/to/tc6-core --out /path/to/tc6-s16-sd
```

`apply_series.py` uses `git apply` (the prepared core carries the display patch
as uncommitted changes, so `git am` is not used) and refuses an already-treated
tree. `--keep-seed` leaves the upstream seed (40).

Verified (2026-10-10): a fresh prepared core + this series is byte-identical to
origin commit `7c29497` for all 10 changed paths, with seed 16.

## Known limits

- GPU is a stub (`EXCLUDE_GPU`): fine for PAL, which draws in software; GPU apps will not render.
- `build_id.mif` missing at build time, so build_id reads 0 (only critical warning).
- The `x_count` clk_vid -> clk_core_49152 hold path is seed-sensitive (+0.076 ns
  min on tc6-s16); an RTL fix is planned but not implemented.
- Nothing here has run on a Pocket or Dock.
