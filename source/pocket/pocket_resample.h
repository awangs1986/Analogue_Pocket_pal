/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PAL_POCKET_RESAMPLE_H
#define PAL_POCKET_RESAMPLE_H
#include <stddef.h>
#include <stdint.h>

/* Fixed-point polyphase windowed-sinc upsampler for 16-bit PCM.
 * 16 taps x 256 phases, Q14 coefficients, int32 accumulation (no 64-bit
 * multiply on rv32).  The filter is defined in source-sample units with its
 * cutoff at 0.9 x source Nyquist, so one table serves every upsampling ratio
 * (5.5k..44.1k -> 48k).  Downsampling falls back to linear interpolation. */
#define PPR_TAPS 16
#define PPR_PHASE_BITS 8

typedef struct {
    uint32_t step;          /* source samples per output sample, Q16 */
    uint64_t pos;           /* Q16 position relative to hist[PPR_TAPS/2-1] */
    int channels;
    int16_t hist[2][PPR_TAPS * 2];  /* sliding history per channel */
    int fill;               /* valid samples in hist (streaming) */
} ppr_stream;

void ppr_init_tables(void);
/* Output frames produced when resampling `in_frames` at the given rates. */
size_t ppr_output_frames(size_t in_frames, int src_rate, int dst_rate);
/* Whole-buffer conversion (interleaved, `channels` = 1 or 2). Returns frames
 * written (<= out_capacity). Edges are zero padded. */
size_t ppr_convert(const int16_t *in, size_t in_frames, int channels,
                   int src_rate, int dst_rate, int16_t *out, size_t out_capacity);

/* Same as ppr_convert for output frames [first, first+count): lets callers
 * slice long effects and keep the audio queue fed in between. */
size_t ppr_convert_part(const int16_t *in, size_t in_frames, int channels,
                        int src_rate, int dst_rate, int16_t *out,
                        size_t first, size_t count);

/* Streaming conversion (AVI soundtrack). */
void ppr_stream_init(ppr_stream *s, int channels, int src_rate, int dst_rate);
/* Consumes all `in_frames`, writes up to out_capacity frames, returns count. */
size_t ppr_stream_push(ppr_stream *s, const int16_t *in, size_t in_frames,
                       int16_t *out, size_t out_capacity);
#endif
