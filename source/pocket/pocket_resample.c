/* SPDX-License-Identifier: GPL-3.0-only
 * See pocket_resample.h. Coefficients are computed once with the FPU at
 * start-up; the per-sample path is integer only. */
#include "pocket_resample.h"
#include <math.h>
#include <string.h>

#define PHASES (1 << PPR_PHASE_BITS)
#define HALF (PPR_TAPS / 2)
#define CUTOFF 0.45            /* cycles per source sample (0.9 x Nyquist) */
#define KAISER_BETA 7.0

static int16_t coef[PHASES][PPR_TAPS];
static int ready;

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0, q = x * x / 4.0;
    for (int k = 1; k < 32; ++k) { term *= q / ((double)k * k); sum += term; }
    return sum;
}

void ppr_init_tables(void)
{
    if (ready) return;
    double i0b = bessel_i0(KAISER_BETA);
    for (int p = 0; p < PHASES; ++p) {
        double frac = (double)p / PHASES, h[PPR_TAPS], sum = 0;
        for (int k = 0; k < PPR_TAPS; ++k) {
            /* tap k sits at source offset (k - HALF + 1) relative to floor(pos) */
            double t = (double)(k - HALF + 1) - frac;
            double x = 2.0 * CUTOFF * t;
            double sinc = fabs(x) < 1e-9 ? 1.0 : sin(M_PI * x) / (M_PI * x);
            double r = t / (double)HALF;
            double w = fabs(r) >= 1.0 ? 0.0 : bessel_i0(KAISER_BETA * sqrt(1.0 - r * r)) / i0b;
            h[k] = 2.0 * CUTOFF * sinc * w;
            sum += h[k];
        }
        int isum = 0;
        for (int k = 0; k < PPR_TAPS; ++k) {
            coef[p][k] = (int16_t)lrint(h[k] / sum * 16384.0);
            isum += coef[p][k];
        }
        coef[p][HALF - 1 + (p >= PHASES / 2)] += (int16_t)(16384 - isum);  /* exact DC gain */
    }
    ready = 1;
}

static inline int16_t sat16(int32_t v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
}

static inline int16_t fir(const int16_t *x, uint32_t frac16)
{
    const int16_t *c = coef[frac16 >> (16 - PPR_PHASE_BITS)];
    int32_t acc = 8192;
    for (int k = 0; k < PPR_TAPS; ++k) acc += (int32_t)x[k] * c[k];
    return sat16(acc >> 14);
}

size_t ppr_output_frames(size_t in_frames, int src_rate, int dst_rate)
{
    if (src_rate <= 0 || dst_rate <= 0) return 0;
    return (size_t)(((uint64_t)in_frames * (uint64_t)dst_rate + (uint64_t)src_rate - 1) / (uint64_t)src_rate);
}

static inline int16_t at(const int16_t *in, size_t frames, int channels, long i, int ch)
{
    return (i < 0 || (size_t)i >= frames) ? 0 : in[(size_t)i * channels + ch];
}

size_t ppr_convert_part(const int16_t *in, size_t in_frames, int channels,
                        int src_rate, int dst_rate, int16_t *out,
                        size_t first, size_t count)
{
    uint32_t step = (uint32_t)(((uint64_t)src_rate << 16) / (uint64_t)dst_rate);
    size_t n = ppr_output_frames(in_frames, src_rate, dst_rate), o, end;
    int16_t win[PPR_TAPS];
    if (channels < 1 || channels > 2 || first >= n) return 0;
    ppr_init_tables();
    end = first + count < n ? first + count : n;
    for (o = first; o < end; ++o) {
        uint64_t pos = (uint64_t)o * step;
        long base = (long)(pos >> 16);
        uint32_t frac = (uint32_t)(pos & 0xffff);
        for (int ch = 0; ch < channels; ++ch) {
            if (src_rate >= dst_rate) {     /* linear fallback for downsampling */
                int32_t a = at(in, in_frames, channels, base, ch);
                int32_t b = at(in, in_frames, channels, base + 1, ch);
                out[o * channels + ch] = (int16_t)(a + (((b - a) * (int32_t)(frac >> 1)) >> 15));
                continue;
            }
            const int16_t *x;
            if (channels == 1 && base - HALF + 1 >= 0 && (size_t)(base + HALF) < in_frames)
                x = in + base - HALF + 1;
            else {
                for (int k = 0; k < PPR_TAPS; ++k)
                    win[k] = at(in, in_frames, channels, base - HALF + 1 + k, ch);
                x = win;
            }
            out[o * channels + ch] = fir(x, frac);
        }
    }
    return end - first;
}

size_t ppr_convert(const int16_t *in, size_t in_frames, int channels,
                   int src_rate, int dst_rate, int16_t *out, size_t out_capacity)
{
    size_t n = ppr_output_frames(in_frames, src_rate, dst_rate);
    return ppr_convert_part(in, in_frames, channels, src_rate, dst_rate, out, 0,
                            n < out_capacity ? n : out_capacity);
}

void ppr_stream_init(ppr_stream *s, int channels, int src_rate, int dst_rate)
{
    ppr_init_tables();
    memset(s, 0, sizeof *s);
    s->channels = channels < 1 ? 1 : channels > 2 ? 2 : channels;
    s->step = (uint32_t)(((uint64_t)src_rate << 16) / (uint64_t)dst_rate);
    /* Pre-fill HALF-1 zeros so the first output is centred on input 0. */
    s->fill = HALF - 1;
}

size_t ppr_stream_push(ppr_stream *s, const int16_t *in, size_t in_frames,
                       int16_t *out, size_t out_capacity)
{
    size_t produced = 0, i = 0;
    while (i < in_frames || s->fill == PPR_TAPS) {
        if (s->fill < PPR_TAPS) {
            for (int ch = 0; ch < s->channels; ++ch) s->hist[ch][s->fill] = in[i * s->channels + ch];
            ++s->fill; ++i;
            continue;
        }
        /* hist[0..TAPS-1] holds source samples floor(pos)-HALF+1 .. floor(pos)+HALF */
        while ((s->pos >> 16) == 0) {
            if (produced >= out_capacity) return produced;
            for (int ch = 0; ch < s->channels; ++ch)
                out[produced * s->channels + ch] = fir(s->hist[ch], (uint32_t)(s->pos & 0xffff));
            ++produced;
            s->pos += s->step;
        }
        while ((s->pos >> 16) > 0 && s->fill == PPR_TAPS) {
            for (int ch = 0; ch < s->channels; ++ch)
                memmove(s->hist[ch], s->hist[ch] + 1, (PPR_TAPS - 1) * sizeof(int16_t));
            --s->fill;
            s->pos -= 1u << 16;
        }
    }
    return produced;
}
