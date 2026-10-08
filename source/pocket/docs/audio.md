# Pocket Ogg audio

## Implemented path

```
ogg/NN.ogg on the data slot
  -> 4 KiB stdio reads
  -> CPU libogg/libvorbis packet decoder (RISC-V F extension)
  -> mono duplication / linear rate conversion
  -> 8192-frame, stereo S16 software reservoir
  -> existing SDLPAL WAV/VOC SFX mix and volume/saturation
  -> at most 2048 queued stereo frames via of_audio_write()
  -> openfpgaOS PCM service
  -> FPGA PCM stream voice / audio output on hardware-mixer cores
```

The FPGA is **not a Vorbis decoder**. This implementation uses CPU Vorbis
decoding and the SDK's existing PCM output, with no new RTL and no claim that
100 MHz RISC-V decode performance has been measured. On OS variants with a
software mixer, the same service can use CPU mixing instead; `OF_HW_MIXER`
means the service is available, not that RTL acceleration is present.

`pocket/audio.c` replaces `sdlpal/audio.c` and `sdlpal/oggplay.c` in this target.
Keep the engine's `sound.c` and `resampler.c` for WAV/VOC effects. No RIX, MIDI,
MP3, Opus, SDL audio callback, pthread, or Linux/HPS audio dependency is used.
The bundled BSD libogg/libvorbis C sources are reused without modification.
The initial build may include all codec C files; function-section garbage
collection removes unused encoder entry points. Link `-lm`.

## Cooperative integration

- Call `PAL_PocketAudioPump()` regularly from frame/event/delay paths.
- Register `PAL_PocketAudioDrain` with `of_file_set_idle_hook()`.
- Never call Pump from the file idle hook or an interrupt.
- `AUDIO_Lock`/`AUDIO_Unlock` suppress pumping while existing SFX lists change.
  There is no audio thread; these are nesting guards, not OS mutexes.
- Initialize through `AUDIO_OpenDevice`, shut down through `AUDIO_CloseDevice`.

Pump produces at most 4096 frames and performs at most 64 decoder/I/O state
transitions per call. An I/O transition reads at most 4096 bytes; a decoder
transition processes one bounded-size packet. These are work-unit limits,
**not hard real-time limits**: the duration of a disk read or one codebook/
audio-packet decode is variable. The idle hook can consume the reservoir
while a read waits. It performs no file reads and no Vorbis operations.

Drain writes at most eight 256-frame chunks per invocation, respects the
reported runtime device capacity, never spins on a full device, and retains
unwritten PCM across partial writes. The queue target is a latency policy,
not an assumed hardware ring size. With the Pocket SDK's much larger native
ring, queued SFX/track-change latency remains about 43 ms rather than seconds.

The 8192-frame software reservoir holds about 171 ms at 48 kHz, in addition
to the device queue. A longer CPU/storage stall can still underrun. Missing
decoded music is replaced with silence while loaded SFX may continue. It
does not replay old ring contents. Current Pocket firmware additionally
uses a true PCM FIFO voice that fades and holds when starved.

## Asset contract and behavior

- Filenames retain SDLPAL's `ogg/01.ogg`, `ogg/02.ogg`, etc. convention.
  The filesystem/data-package layer must register the nested relative path.
- Accepted: one Ogg Vorbis logical stream, mono or stereo, integer source
  rate 8000–48000 Hz. Recommended production assets: 48 kHz stereo Vorbis.
- Mono is duplicated to L/R. Lower rates are linearly upsampled using a
  rational phase accumulator, without drift. No downsampling filter is
  supplied, so rates above 48 kHz are rejected instead of aliased.
- Multichannel/multiplexed streams are rejected. Chained streams stop at
  the first logical stream's EOS; concatenated files are not a playlist.
- Headers and individual packets are limited to 256 KiB; continued-packet
  accumulation and resynchronization reads are also bounded. PCM storage
  is fixed-size and the complete music track is never loaded into RAM.
- Codec codebook/DSP allocations are the existing libvorbis allocations,
  dependent on stream headers; they do **not** have a custom heap quota.
  This is a bounded-streaming implementation, not a hardened sandbox for
  hostile media. Use trusted locally prepared assets. Allocation/CPU peak
  and the actual asset set still need on-device qualification.
- EOS granule trimming is handled by libvorbis. Nonlooping music drains
  the buffered tail once and then stops. Looping rewinds and rebuilds the
  decoder while preserving queued PCM; playback repeats the same trimmed
  samples. Very slow storage can still make a loop boundary underrun.
- Empty, missing, truncated, bad-checksum, unsupported-rate/channel files
  stop safely with a serial diagnostic; the game and SFX continue.
- Music-disable pauses reservoir consumption/decoding; reenabling resumes.
  Volume zero keeps playback advancing. Per-channel sums saturate to S16.
- A positive requested fade gives a bounded fade-in (maximum 10 seconds).
  Stop/track replacement is immediate after already submitted PCM. There
  is no two-track crossfade; the original Ogg backend ignored fade time.
- CD replacement files use SDLPAL's `10000 + track` convention, e.g.
  `ogg/10001.ogg`, when CD emulation is enabled. Only one music stream is
  active. `AUDIO_PlayCDTrack(-2)` safely reports current CD state.
- AVI audio is outside this initial target. The parent port disables AVI
  playback explicitly rather than advertising silent video support.

## Tests

From the repository root:

```
python3 pocket/tests/audio_test.py
ASAN_OPTIONS=detect_leaks=0 \
  PPA_TEST_CFLAGS='-O1 -fsanitize=address -fno-omit-frame-pointer' \
  python3 pocket/tests/audio_test.py
```

Requirements: host C compiler, installed FFmpeg with libvorbis encoding, and
the system `libvorbisfile.so.3`, `libvorbis.so.0`, `libogg.so.0`. Tests create
temporary sine-wave files only; no game assets are bundled. Reference PCM
comes from the host's independently linked libvorbisfile. FFmpeg is used to
generate fixtures, not to establish exact Vorbis trim length: its demuxer
can discard an initial block differently for short fixtures.

With the sibling `openfpgaSDK` checkout (or `SDK_ROOT` pointing to it), the
same suite also builds a headless adapter test using the real SDK's SDL type
declarations and stub PCM/timer services. It checks exact PCM sequence across
short/zero device writes, reentrancy, nested locks, small runtime capacities,
shutdown, and CD status after immediate/deferred Ogg failure. This is a service
simulation, not a hardware-timing test.

Covered: native-rate stereo PCM, mono duplication, 44.1/22.05/8 kHz conversion,
exact EOS frame counts, loop repetition, ring wrap/full backpressure, zero
work budgets, per-pump budgets, underrun reads without stale data, corruption,
truncation, and rejection of 96 kHz and surround input. Native tests do not
prove SDK device timing, SD-card behavior, full-game integration, or RISC-V
throughput.

The AddressSanitizer suite passes with leak detection disabled. The current
execution environment prevents LeakSanitizer from running under ptrace, so
there is no verified leak-sanitizer result. An additional UBSan run found
existing signed-left-shift diagnostics in vendored `bitwise.c:397/399`,
`sharedbook.c:402`, and `framing.c:63`; the codec was not modified and a clean
UBSan result is **not** claimed. These are baseline codec findings, not a
reason to suppress sanitizer diagnostics in a release-qualification run.

## On-device acceptance and escalation

`PAL_PocketAudioDiagnostics()` exposes measured cumulative decode wall time,
maximum decoder-call duration, maximum gap between pumps, output/silence
frames, reservoir fill, format, and software/device starvation counters.
A concise serial report is printed every five seconds while a track is
selected. The hardware-starvation counter is an observation of near-empty
queue episodes, not an electrical audio-glitch detector. The initial startup
can include a software underrun before enough headers/audio are decoded.

1. Use real target hardware and the pinned SDK/core, with legally supplied
   game assets. Collect traces for 48 kHz mono/stereo, title/scene changes,
   battle, menus, long SD loads, music looping, and simultaneous SFX.
2. Ignore/reset startup transients when comparing steady-state counters.
   Require no steadily increasing underruns under representative play;
   verify the reservoir recovers after loads and sound latency is acceptable.
3. Compare decode wall time per emitted audio duration, maximum pump gap,
   memory high-water, and frame pacing. A desktop benchmark is not evidence
   for the 100 MHz target. The timer includes file wait time, intentionally.
4. First remove accidental repeated reads/decodes, improve cooperative pump
   coverage, and choose valid 48 kHz assets to avoid unnecessary resampling.
   Measure before changing buffering or reducing codec complexity.
5. If measured CPU Vorbis performance cannot coexist with the game, a true
   FPGA Vorbis decoder is a separate RTL project: packet/codebook parsing,
   entropy decode, residue/floor reconstruction, inverse coupling, IMDCT,
   window/overlap, granule trim, DMA, and flow control must be implemented
   and verified against the same PCM reference. The current PCM stream or
   hardware mixer alone does not satisfy that fallback.
