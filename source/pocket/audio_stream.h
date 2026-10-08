/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PAL_POCKET_AUDIO_STREAM_H
#define PAL_POCKET_AUDIO_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define PPA_OUTPUT_RATE 48000u
#define PPA_RING_FRAMES 8192u
#define PPA_READ_BYTES 4096u
#define PPA_PACKET_LIMIT (256u * 1024u)

typedef struct ppa_stream ppa_stream;
typedef enum ppa_state {
    PPA_STOPPED, PPA_HEADERS, PPA_PLAYING, PPA_FINISHED, PPA_ERROR
} ppa_state;

typedef struct ppa_stats {
    uint64_t bytes_read, source_frames, output_frames;
    uint32_t loops, underrun_reads, decode_steps;
    unsigned source_rate, source_channels;
} ppa_stats;

ppa_stream *ppa_create(void);
void ppa_destroy(ppa_stream *stream);
/* Takes ownership of file, including on failure. Does not read or decode. */
int ppa_start(ppa_stream *stream, FILE *file, int loop);
void ppa_stop(ppa_stream *stream);
void ppa_set_loop(ppa_stream *stream, int loop);
/* Cooperative producer. Max output frames and codec/I/O state transitions.
 * One transition can read <=4096 bytes or decode one <=256KiB packet.
 * This bounds work units, not elapsed time of a disk read/codec packet. */
size_t ppa_pump(ppa_stream *stream, size_t frame_budget, unsigned step_budget);
/* Consumer never reads files or invokes the codec. Copies available frames;
 * does not pad missing frames. Caller may supply silence for the remainder. */
size_t ppa_read(ppa_stream *stream, int16_t *stereo, size_t frames);
size_t ppa_queued(const ppa_stream *stream);
ppa_state ppa_status(const ppa_stream *stream);
const char *ppa_error(const ppa_stream *stream);
const ppa_stats *ppa_get_stats(const ppa_stream *stream);

/* Engine adapter: invoke Pump from frame/event/delay loops. Drain is suitable
 * for the SDK file-idle hook and only consumes already decoded PCM. */
void PAL_PocketAudioPump(void);
void PAL_PocketAudioDrain(void);

typedef struct pal_pocket_audio_diagnostics {
    uint64_t decode_us, output_frames, silence_frames;
    uint32_t pump_calls, max_decode_us, max_pump_gap_us;
    uint32_t hardware_starvations, software_underruns;
    uint32_t source_rate, source_channels, pcm_queued;
} pal_pocket_audio_diagnostics;
const pal_pocket_audio_diagnostics *PAL_PocketAudioDiagnostics(void);

#endif
