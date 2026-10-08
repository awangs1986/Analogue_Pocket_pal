# Native display and input contract

## What this port renders

The game, backup and presentation surfaces are all **320 × 200, 8-bit indexed**.
Their 256-color SDL palette is shared. `pocket/video.c` replaces only the Pocket
build's `sdlpal/video.c`; the existing MiSTer target is unaffected.

The backend requests exactly `320 × 200 / OF_VIDEO_MODE_8BIT` from the runtime,
then checks the returned mode. Unsupported runtimes fail video startup rather
than silently converting the canvas to 320 × 240 or 320 × 224. The backend does
not call the SDK SDL window/renderer presentation path: that path can silently
scale a surface when the requested mode is unavailable.

Each presentation copies 320 visible bytes per row into the current runtime
framebuffer using its reported stride. No framebuffer addresses are hardcoded.
A separate native-size staging surface preserves dirty-rectangle history while
all three hardware buffers rotate. Palette changes upload all 256 `0x00RRGGBB`
entries, including palette-only fades. The original dissolve, palette-bank fade
and scripted screen shake operate in native pixel coordinates. Shake can move
pixels off the canvas as the game intends; it never changes the canvas size.
Audio is pumped before/after presentations and during transition event loops.

The SDK uses SDL2-compatible surface types even though it also exposes a subset
of SDL 1.2 calls. No private SDL layout or custom pixel format is introduced.
Repeated `VIDEO_UpdateSurfacePalette` calls avoid rebinding an already-shared
palette because the pinned SDK increments the reference count on such a bind.

Truecolor Windows AVI playback is disabled in the platform configuration.
`VIDEO_DrawSurfaceToScreen` explicitly rejects a non-native or non-indexed
surface. DOS/RNG animation remains on the normal indexed engine path. Desktop
resize/fullscreen/depth controls cannot change the canvas. Screenshot saving is
not implemented and reports that limitation; the SDK BMP writer is also a stub.

## Scaler and Display Modes

`pocket/config/video.json` preserves the runtime's eight-slot order. **Slot 1**
is 320 × 200 with **4:3** aspect on both handheld and Dock. Slot 0 remains the
320 × 240 runtime/terminal slot. Do not remove slots or renumber the game to
slot 0 without changing the runtime and FPGA together. The other slots are ABI
compatibility entries, not alternative SDLPAL rendering sizes.

The default configuration advertises only **CRT Trinitron (`0x10`)**, besides
the OS's ordinary unfiltered mode. `0x00` must not be listed in JSON. Analogue's
spec says CRT scaling preserves the configured aspect; LCD scaling may force
separate integer factors and lose 4:3 for a 320 × 200 source. Generic color LCD
modes `0x30`/`0x40` and the system-specific LCD modes are therefore excluded.
Grayscale LCD modes `0x20`–`0x23` additionally require grayscale RGB output and
the display-notify response. No software stretch or horizontal duplication is
used. [Official video.json specification](https://www.analogue.co/developer/docs/openfpga/core-definition-files/video-json).

For command `0x00B8`, parameter bit 0 requests grayscale, bits 15:8 identify the
mode, and the required grayscale response word is `0x444D`.
[Official Host/Target Commands specification](https://www.analogue.co/developer/docs/openfpga/host-target-commands).

The inspected core merely acknowledges `0x00B8`; it neither latches that request
nor returns the required grayscale word or converts output. Advertising those
modes would overstate support. A future implementation needs a coordinated RTL,
runtime/API and palette/scanout change, with live mode-switch and return-to-color
tests. This port does not modify upstream SDK/core files or include an untested
patch claiming to complete that work.

Spec review: 2026-10-05. Source pins inspected:

- SDK `a408ddc12aed0dfaa4aa22c06af82f829db77126`: `src/sdk/of_sdl2.c`,
  `src/sdk/include/of_video.h`, `src/sdk/include/of_input_types.h`,
  `src/sdk/platforms/pocket/templates/video.json`.
- Core `8318f25066b0b75d2e34081919dfc337ac6abf73`:
  `src/firmware/os/targets/pocket/video.c` (scaler slot selection),
  `src/fpga/targets/pocket/core_bridge_cmd.v` (`16'h00B8` handler),
  `src/fpga/targets/pocket/core_top.v` (scaler dimensions).

## Controls

The native `pocket/input.c` replaces the engine input file in this target.
It calls `of_input_poll` once per `PAL_ProcessEvent`; it does not combine the
SDK's synthesized pad keyboard events with a second controller event stream.

| Input | Action |
|---|---|
| D-pad | Move / menu navigation |
| A | Search, interact, confirm |
| B | Back / cancel / game menu |
| X | Use item shortcut |
| Y | Status shortcut |
| L / R | Page up / down |
| Select | Auto-battle shortcut |
| Start | Game menu / cancel |
| Docked L2 / R2 | Page up / down |
| Left analog stick | Direction, threshold ±12000 |

The Pocket's OS button is not remapped. Both runtime pad slots can operate this
single-player game. L3/R3, right stick, mouse and touch do not add actions. All
normal actions remain available through the game menus. No new quit shortcut
can accidentally terminate the game or discard a save.

A dock keyboard retains the engine's arrows/keypad navigation, Enter/Space/Ctrl
confirm, Escape/Insert/Alt cancel, page/home/end keys and R/A/D/E/W/Q/F/S battle
shortcuts. Handheld use needs no keyboard. Inputs from both pads and keyboard
are merged before edge detection, so releasing one source cannot release an
action still held by another. Latched complete taps are consumed on newer
runtimes. Held keys repeat after 200 ms and then at 75 ms if enabled; the timer
comparison handles 32-bit rollover. The most recently pressed direction wins.
Input filters receive canonical SDL key edges and may consume a press until
release. Audio is pumped on each event-processing pass.

## Verification and remaining hardware checks

Run the focused asset-free tests from the repository root:

```sh
SDK_ROOT=../openfpgaSDK python3 -m unittest discover -s pocket/tests -p 'test_video.py' -v
SDK_ROOT=../openfpgaSDK python3 -m unittest discover -s pocket/tests -p 'test_input.py' -v
```

The tests compile the real port backend C files with the SDK's SDL/input types,
public HAL test doubles, and undefined-behavior sanitization. They cover native
mode rejection, exact RGB palette bytes, padded pitches and guarded rows,
rotating draw pages, partial frames, transitions, shake, allocation failure and
palette lifetime; plus pad/keyboard mappings, analog threshold, second-pad
control, release/unplug, repeat timing, tick rollover, short taps and filters.
They do **not** emulate the FPGA, verify actual HDMI/LCD output or prove the
runtime's scanout/cache implementation. Both backend objects also compile for
`rv32imafc / ilp32f` with the official Debian cross-compiler.

Before calling this hardware-validated:

1. Show an asset-free border/grid with markers on row 0 and row 199. Confirm
   neither edge disappears and there is no 24/40-line padding.
2. Test normal and CRT mode on Pocket and Dock; measure the active image as 4:3.
   A native test ellipse with horizontal/vertical radii 60/50 should be round
   after the intended 5:6 pixel-aspect correction.
3. Exercise all palette entries, palette-only fades, dissolve, shake and fast
   partial updates. Look for stale pages, tearing and lost bottom rows.
4. Test gameplay/menu inputs, simultaneous and rapid taps, held-repeat, dock
   keyboard unplug and both pads. Confirm music continues during transitions.
5. Select/unselect CRT while running; confirm normal color returns and no
   grayscale-only modes are offered. LCD filters remain explicitly unsupported
   by this configuration, pending a separate aspect/grayscale design.
