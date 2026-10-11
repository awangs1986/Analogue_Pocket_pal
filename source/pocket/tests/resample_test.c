/* SPDX-License-Identifier: GPL-3.0-only
 * Fixed-point polyphase resampler + SFX cache contract (host build). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pocket_resample.h"
#include "sfx_cache.h"

static int drains;
void PAL_PocketAudioDrain(void) { ++drains; }

static double tone_db(const int16_t *x, size_t n, double f, double rate)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));
        re += x[i] * w * cos(2 * M_PI * f * i / rate);
        im -= x[i] * w * sin(2 * M_PI * f * i / rate);
    }
    return 10 * log10((re * re + im * im) / ((double)n * n) + 1e-30);
}

int main(void)
{
    enum { SRC = 8000, N = 8000 };
    static int16_t in[N], out[N * 7], streamed[N * 7];
    size_t n, m, i;
    /* DC gain is exact for every phase. */
    for (i = 0; i < N; ++i) in[i] = 12345;
    n = ppr_convert(in, N, 1, SRC, 48000, out, sizeof out / sizeof out[0]);
    assert(n == (size_t)N * 6);
    for (i = 100; i < n - 100; ++i) assert(out[i] == 12345);
    /* 3 kHz tone at 8 kHz: the ZOH image at 5 kHz must be >= 50 dB down. */
    for (i = 0; i < N; ++i) in[i] = (int16_t)lrint(16000 * sin(2 * M_PI * 3000.0 * i / SRC));
    n = ppr_convert(in, N, 1, SRC, 48000, out, sizeof out / sizeof out[0]);
    {
        double sig = tone_db(out + 1000, n - 2000, 3000, 48000);
        double img1 = tone_db(out + 1000, n - 2000, 5000, 48000);
        double img2 = tone_db(out + 1000, n - 2000, 11000, 48000);
        printf("fir: tone %.1f dB, images %.1f / %.1f dB below\n", sig, sig - img1, sig - img2);
        assert(sig - img1 > 50 && sig - img2 > 50);
    }
    /* Streaming == whole-buffer for the interior, any chunking. */
    {
        ppr_stream s;
        size_t done = 0, at = 0, step = 37;
        ppr_stream_init(&s, 1, 22050, 48000);
        m = ppr_convert(in, N, 1, 22050, 48000, out, sizeof out / sizeof out[0]);
        while (at < N) {
            size_t k = N - at < step ? N - at : step;
            done += ppr_stream_push(&s, in + at, k, streamed + done, sizeof streamed / sizeof streamed[0] - done);
            at += k; step = step * 3 % 101 + 1;
        }
        /* stream holds back PPR_TAPS/2 source frames of look-ahead */
        assert(done + (PPR_TAPS / 2) * 48000 / 22050 + 2 >= m && done <= m);
        for (i = 0; i < done; ++i) assert(streamed[i] == out[i]);
    }
    /* Stereo keeps channels apart. */
    {
        int16_t st[2000], so[2000 * 3];
        for (i = 0; i < 1000; ++i) { st[2 * i] = 1000; st[2 * i + 1] = -2000; }
        n = ppr_convert(st, 1000, 2, 22050, 48000, so, 3000);
        for (i = 50; i < n - 50; ++i) assert(so[2 * i] == 1000 && so[2 * i + 1] == -2000);
    }
    /* Cache: U8 conversion, hit, refcount-protected LRU eviction, drain slices. */
    {
        static uint8_t u8[60000];
        PAL_PocketSfx *a, *b, *c;
        uint32_t entries, bytes, hits, misses;
        memset(u8, 128 + 64, sizeof u8);
        a = PAL_PocketSfxAcquire(1, u8, sizeof u8, 8, 1, 11025, 48000);
        assert(a && a->channels == 1 && a->frames == ppr_output_frames(sizeof u8, 11025, 48000));
        /* first slice is converted at once, the rest progressively by the pump */
        assert(a->pcm[100] == 64 << 8);
        for (i = 0; i < 64; ++i) PAL_PocketSfxPump();
        assert(a->pcm[a->frames / 2] == 64 << 8 && a->pcm[a->frames - 100] == 64 << 8);
        b = PAL_PocketSfxAcquire(1, NULL, 0, 0, 0, 0, 0);
        assert(b == a && a->refs == 2);
        PAL_PocketSfxRelease(b);
        assert(PAL_PocketSfxAcquire(2, NULL, 0, 0, 0, 0, 0) == NULL);
        for (i = 2; i < 40; ++i) {          /* ~260 KB each -> forces eviction */
            c = PAL_PocketSfxAcquire((int)i, u8, sizeof u8, 8, 1, 11025, 48000);
            assert(c);
            PAL_PocketSfxRelease(c);
        }
        PAL_PocketSfxStats(&entries, &bytes, &hits, &misses);
        assert(bytes <= PAL_POCKET_SFX_CACHE_BYTES + a->bytes && hits == 1 && misses == 39);
        assert(PAL_PocketSfxAcquire(1, NULL, 0, 0, 0, 0, 0) == a);   /* still referenced */
        PAL_PocketSfxRelease(a); PAL_PocketSfxRelease(a);
        assert(PAL_PocketSfxAcquire(2, NULL, 0, 0, 0, 0, 0) == NULL); /* oldest evicted */
        printf("cache: %u entries, %u bytes\n", (unsigned)entries, (unsigned)bytes);
    }
    puts("resampler + sfx cache PASS");
    return 0;
}
