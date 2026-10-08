# Copyright-free hardware probe

The separate probe runs the actual Vorbis decoder/archive layer on RISC-V and
shows a synthetic native 320×200 pattern. It has no PAL game content and never
writes save/config files. It is a useful first hardware test, not a full game
benchmark: the game logic, battles, SFX mixing and real asset loads cost more CPU.

Build and prepare from the repository root:

```sh
make -C pocket probe CROSS=riscv64-unknown-elf- -j4
python3 pocket/tools/make_probe_assets.py pocket/build/probe/pal.pak
python3 pocket/tools/package.py --sdk ../openfpgaSDK \
  --elf pocket/build/probe/app.elf --probe-assets pocket/build/probe/pal.pak \
  --out pocket/build/probe-package
```

The generator requires installed FFmpeg with libvorbis encoding. It makes only
four seconds of 440 Hz left / 660 Hz right sine tones, loops decoded OGG, and
keeps the original compressed OGG in the archive. Its output is safe to share.
The probe package has separate core ID `awangs1986.PALProbe` and platform asset
folder `sdlpalprobe`; it does not replace the game core or share its save folder.

On a Pocket, after separately choosing to install the candidate, launch the
probe instance. Confirm all four white edges, the one-pixel top-left checkerboard,
and the gray center shape. At the declared 4:3 aspect, the ellipse in source pixel
coordinates should appear round (pixel aspect 5:6). Press Start to show/hide
runtime status. Switch normal/CRT using the Pocket display menu and photograph
both. The probe does not implement custom shaders or monochrome modes.

Status reports CPU clock, input OGG rate/channels, maximum decoder pump duration,
maximum main-loop gap, decode time per thousand wall-time units, starvation
observations, and loop count. Leave it running for at least 30 minutes, switch
menus several times, and save the measured values and package hash. Software
counters alone do not detect all audible glitches: listen to left/right channels.
`decode budget` includes storage waits. Initial startup/header fill is separate
from steady-state testing.

No device run was performed during this cloud implementation. A successful
probe result would still leave the full-game acceptance checklist open.
