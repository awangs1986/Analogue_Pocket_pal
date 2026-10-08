# Pocket built-in Display Modes follow-up

## Status and scope

**Grayscale source implementation and behavioral simulation passed; its new
FPGA build and physical acceptance are still required.** The separate optional
`color-v1.video.json` profile can use the existing pinned runtime without a
Quartus rebuild. Its device appearance is not yet verified. The actual v1
package still uses its original `os25` bitstream and CRT-only `video.json`;
no optional profile is automatically copied by the existing packager.

The game still renders a **320×200** canvas in scaler slot **1**, with **4:3**
as the normal/default and Dock aspect request. Slot 0 remains the runtime's
320×240 boot/terminal slot. No framebuffer duplication, resampling, crop, canvas
change, SDK modification or old-bitstream relabeling is part of this change.

Inputs:

- SDK: `a408ddc12aed0dfaa4aa22c06af82f829db77126`
- Runtime source: `618a3eb985759a4154115109c2c8036271252888`
- Variant to rebuild: `os25`
- Patch and SHA-256: `../fpga/display-modes.patch` and `../fpga/source.json`

The core source guidance file at this revision is `CLAUDE.md` (the core has no
`AGENTS.md`). It requires isolated builds, preserving the SDK ABI, and runtime
binaries published as one coherent build. The patch is additive to the Pocket
APF output path; it makes no CPU-visible register or SDK ABI changes.

## APF contract and implementation

The official [Host/Target Commands specification](https://www.analogue.co/developer/docs/openfpga/host-target-commands)
defines command `0x00B8`: the parameter's bits 15:8 identify the selected mode,
and bit 0 requests grayscale. Grayscale acceptance uses response data `0x444D`.

The real bridge handler now:

1. Latches the requested mode ID and grayscale bit independently, committing the
   selected mode only after a successful transaction. It does not
   infer grayscale from an ID range; future IDs can use the same request bit.
2. Clears stale response data and reports busy while awaiting a fresh tagged
   transaction acknowledgment from the video domain. Even repeated same-valued
   requests need their own acknowledgment; gray-value equality alone is unsafe.
3. Returns `0x0000444D` at the host response pointer (`0x40`, read through
   `0xF8000040`) only after the applied grayscale flag has synchronized back.
   The command/status word separately becomes `0x4F4B0000`.
4. Returns a zero response for ordinary/color modes, clearing old `0x444D`.
5. Preserves the mode selection across game warm resets.
6. If video confirmation never arrives, fails after 7,425,000 bridge clocks
   (100 ms at 74.25 MHz), leaves the response zero, returns the existing error
   status `0x4F4BFFFF`, and enters cancellation recovery. It keeps the canceled
   mailbox payload/toggle immutable until the late acknowledgment arrives,
   then sends and waits for an explicit rollback transaction to the previously
   committed grayscale state. Until rollback completes, subsequent `00B8`
   commands fail promptly with no `0x444D`; no stale value/toggle can become a
   false success. If video remains stopped, recovery remains pending. On
   recovery, the canceled setting can appear for one frame before rollback,
   but no new selection is falsely acknowledged during that interval.

The new `pal_display_mode_video` output stage sits after `video_rgb_core`, the
existing Pocket blanking mux. It adds one matched pixel-clock cycle to RGB,
DE, SKIP, VS and HS. It does not change the existing Analogizer path. Grayscale
is applied only when DE is asserted; frame feature words, scaler-slot commands
and all other blanking RGB values remain bit-exact.

Conversion is full-range integer luma:

```text
Y = floor((77*R + 150*G + 29*B + 128) / 256)
output = (Y, Y, Y)
```

The coefficients sum to 256: every neutral level 0…255 is preserved, including
exact black and white. Explicit 16-bit shift/add operands avoid truncation and
require no framebuffer, block RAM or general multiplication operator. Normal,
CRT and color LCD modes retain exact RGB values through this stage.

The crossing is a one-outstanding toggle mailbox. The bridge holds the
payload until its tagged acknowledgment, including after a timeout. The toggle
crosses through three registers and the grayscale payload through two; after
observing the synchronized toggle, the video stage captures its settled payload
into a pending register. Application occurs on a later VS boundary with DE low,
preventing mixed color/grayscale rows in one frame. While scanout is held in
reset, there may be no VS; a reset bypass allows application before Reset Exit.
The return acknowledgment crosses through three registers, the applied value
through two, and the bridge checks both. Tags are never reused before draining
the canceled transaction and its rollback. This avoids the canceled-gray /
new-color ABA race when a stopped video clock resumes.

With running 60 Hz video, expected completion is within one frame plus CDC and
bridge cycles (approximately 16.7 ms plus those cycles). **The public APF command
spec does not specify a host timeout.** The local 100 ms guard does not prove
Pocket accepts that delay or error code on hardware. Verify actual notification
acceptance and mode transitions on the device before release.

## Optional profiles and truthful aspect behavior

The official [video.json specification](https://www.analogue.co/developer/docs/openfpga/core-definition-files/video-json)
limits each file to 16 Display Mode entries. `0x00` is normal/no Display Mode;
it is a host-notification value, never an advertised JSON entry.

### Existing-runtime color-input option

`color-v1.video.json` advertises CRT plus 15 LCD modes that accept RGB input:
`0x10`, `0x30–0x32`, `0x40–0x42`, `0x51–0x52`, `0x61–0x63`, `0x71–0x72`,
`0x81–0x82`. This is exactly 16 entries. It excludes every grayscale-required
ID (`0x20–0x23`), so the existing v1 RGB output and its ordinary `00B8` ACK can
serve this profile without the new grayscale RTL or `0x444D` response.

This profile is **configuration-ready, hardware-unverified**. It is optional
and not installed or included in the v1 package automatically. It still needs
explicit opt-in to possible LCD aspect differences and Pocket/Dock visual
checks. The default remains unchanged. Matrix/VFD stay in the broader rebuilt
runtime profiles rather than displacing an LCD family from this 16-entry list.

### Full profiles requiring the new grayscale runtime

The following three profiles include grayscale modes and together cover all
22 currently documented IDs:

- `generic.video.json`: CRT `0x10`, grayscale LCD `0x20`, reflective color LCD
  `0x30`, backlit color LCD `0x40`, Neon Matrix `0xE0`, Vacuum Fluorescent `0xE1`.
- `nintendo-sega.video.json`: the six above plus GB DMG/Pocket/Light
  `0x21–0x23`, GBC/LCD+ `0x31–0x32`, GBA/SP101 `0x41–0x42`, GG/GG+
  `0x51–0x52` (15 entries total).
- `snk-nec-atari.video.json`: the generic six plus NGP/NGPC/NGPC+
  `0x61–0x63`, TurboExpress/PC Engine LT `0x71–0x72`, Lynx/Lynx+
  `0x81–0x82` (13 entries total).

Generic LCD families are the recommended starting profile. Originals are
optional visual variations, not a claim that PAL recreates those consoles.
Switching profiles is a configuration change, not a runtime shader loader;
only one profile is a core's active `video.json` at a time.

CRT honors the requested aspect ratio. LCD modes independently integer-scale
width and height, so the same 4:3 request is **not guaranteed** to be respected.
Exact 4:3 is possible for some scale pairs: 320×5 by 200×6 gives 1600×1200.
The chosen Pocket/Dock scaling and presentation still require physical checks.
The source 320×200 is not a square-pixel 4:3 canvas, and an unchanged JSON aspect
field cannot override the LCD scaler's documented behavior. Matrix/VFD final
appearance also needs device checks; no unverified aspect promise is made.

The strict default therefore remains the existing 4:3 normal/CRT configuration.
Every LCD profile requires explicit opt-in to its possible aspect difference.
`color-v1.video.json` is usable with the existing runtime under that opt-in;
the three profiles containing monochrome modes also require a rebuilt and
verified runtime. This change never enables monochrome modes on the old bitstream.

## Source-only verification performed

With Icarus Verilog 12.0 from an official Debian package (package hash in
`../fpga/source.json`):

```sh
CORE_ROOT=/path/to/openfpgaCore PAL_REQUIRE_RTL=1 \
  python3 -m unittest pocket.tests.test_display_modes -v
```

The test uses the real patched `core_bridge_cmd.v`, real new grayscale stage,
and the exact upstream synchronizer implementation. Only the unrelated vendor
BRAM datatable has a simulation model. The bridge and video use unrelated
14 ns and 40 ns clocks by default. A persistent eight-case clock/phase matrix
repeats the whole bench at additional period ratios and initial clock levels;
no hidden forcing of internal FSM state is used.

Verified:

- Exact-base patch application and reverse patch round-trip, without modifying
  the source checkout; new QSF source registration and final-output wiring.
- 13,427 output samples: 7,201 grayscale, 4,280 color, 1,946 blank/control.
- Every neutral level, RGB primaries and thousands of deterministic color samples;
  output luma is compared with an independent full-width integer reference.
- All eight scaler-slot words, arbitrary blanking RGB and aligned sidebands.
- Both bridge endian modes, busy vs response location, boot/reset bypass,
  normal→monochrome→color transitions, repeated notifications, warm reset,
  authoritative request bit and unrelated OS notifications.
- Missing VS and stopped video clock terminate with an error, never stale
  `0x444D`; tagged cancellation and rollback complete before new work succeeds.
  Regressions stop video both before and after request capture, try new color
  requests while recovery is pending, and prove late canceled grayscale cannot
  change pixels after a subsequent successful color acknowledgment. A sweep
  applies VS 1–8 bridge cycles before timeout, in both transition directions.
- Nine deliberate behavioral mutations are caught: missing grayscale,
  conversion of blanking metadata, broken full-range luma, misaligned DE,
  premature ACK, ignored request bit, mid-frame switching, accepting new work
  during cancellation, and removed timeout.
- Optional profiles retain all eight scaler slots, preserve the 320×200/4:3
  slot-1 declaration, contain no duplicate/zero IDs, stay within 16 entries,
  and cover all 22 documented IDs. The existing-runtime profile is checked for
  exactly the 16 allowed RGB-input IDs and the absence of `0x20–0x23`; catalog
  runtime requirements must match each profile's monochrome content.

Icarus reports upstream modules without explicit timescales; all timed stimulus
is in the testbench, so this warning does not change the behavioral checks.
The patch converts the existing three-positional-argument `synch_3` instance
to named ports with its unused edge outputs open, allowing standards-strict
Icarus elaboration against the real five-port upstream module.

This is behavioral simulation, **not** Quartus device elaboration or physical
timing analysis. It does not prove metastability MTBF, routing, resource fit,
Pocket frame interpretation, LCD aspect, visual quality or Dock behavior.

## Build and release gate

The following FPGA build gate applies to the grayscale patch and profiles
containing `0x20–0x23`. It is not a prerequisite for the separate v1 color-input
configuration. That option still requires explicit profile selection and
Pocket/Dock visual/aspect acceptance; no installation is performed here.

1. Prepare a separate tree with `pocket/fpga/prepare_runtime.py`; leave the pinned
   SDK and v1 runtime untouched. It creates a source-only marker, not a runtime
   manifest. Materialize the exact pinned submodule dependencies in that new
   tree using the runtime's normal workflow.
2. In the separate tree, read its build guidance and use its supported isolated
   Quartus Prime 25.1 container workflow:

   ```sh
   make -C src/fpga/targets/pocket full VARIANT=os25 JOB=pal-display-os25
   make -C src/fpga/targets/pocket report VARIANT=os25 JOB=pal-display-os25 FULL=true
   ```

   CPU generation, firmware, bitstream, reports and provenance must come from
   that patched tree. A CPU netlist or an old `.rbf_r` is not the new runtime.
3. Inspect Quartus warnings and full setup/hold timing for the output stage,
   CDC synchronizer identification and clock constraints. The existing `os25`
   profile already uses all 308 M10K blocks; confirm the patch does not add RAM,
   confirm ALM/register usage and any inferred resources, and address timing
   failures rather than assuming simulation is sufficient.
4. Run the runtime's bridge/video regression suite, then repeat PAL's host and
   source-RTL tests. Capture exact tool versions, source commit, patch SHA-256,
   variant/seed, bitstream/OS hashes and reports as release evidence.
5. Use a separately identified candidate built from that coherent runtime only
   for authorized hardware testing. Verify boot and 320×200 slot 1, 4:3 normal
   and CRT, grayscale ramp/primaries, all offered LCD families, frame-safe
   switching/repeat switching, warm reset, and Dock undock/redock. Check APF
   control metadata and the notification response on device. Record actual
   aspect for each LCD family and both handheld/Dock output.
6. Only after build and acceptance pass, introduce an explicitly versioned
   runtime/pin/manifest update and a deliberate optional profile selection in
   packaging. Existing packaging/hash checks must remain fail-closed; do not
   hand-mix the new bitstream into the v1 manifest or bypass pin verification.

No push, PR, deployment or SD-card write is authorized by these source steps.
As of this source verification, Quartus is not installed in the execution
environment, and no patched bitstream or hardware acceptance exists.
The later authorized download/install attempt and exact resume requirements
are recorded in [the Quartus build status](quartus-build-status-2026-10-05.md).

### Boot-image build guard

During local preparation, the pinned upstream Makefile returned success even
when `hexdump` was absent, creating an all-NOP MIF with zero real boot words.
This was a build-dependency failure, not a usable boot image. After supplying
GNU `hexdump` from the official Debian `bsdextrautils` package, the MIF was
rebuilt and byte-compared against its 15,480-byte boot.bin (3,870 real words).
The v1 SDK/runtime was never replaced with this intermediate output.

Before Quartus uses a locally rebuilt boot image, require:

```sh
python3 pocket/fpga/verify_boot_image.py \
  /path/to/core/src/firmware/os/bld/pocket/firmware.mif \
  /path/to/core/src/firmware/os/bld/pocket/boot.bin
```

The validator checks depth, coverage, duplicate/out-of-range addresses, exact
little-endian boot bytes and NOP padding. Its regression includes the all-NOP
false-pass case. Successful firmware compilation alone is not enough.
