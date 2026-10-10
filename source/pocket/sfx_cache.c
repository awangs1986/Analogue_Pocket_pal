/* SPDX-License-Identifier: GPL-3.0-only */
#include "sfx_cache.h"
#include "pocket_log.h"
#include "pocket_resample.h"
#include <stdlib.h>
#include <string.h>

/* Keeps the 48 kHz output queue fed while a long effect is converted. */
extern void PAL_PocketAudioDrain(void);
enum { SLICE_FRAMES = 4096 };

static PAL_PocketSfx *head, *tail;     /* head = most recently used */
static uint32_t used_bytes, n_entries, n_hits, n_misses;

static void unlink_entry(PAL_PocketSfx *e)
{
    if (e->prev) e->prev->next = e->next; else head = e->next;
    if (e->next) e->next->prev = e->prev; else tail = e->prev;
    e->prev = e->next = NULL;
}
static void push_front(PAL_PocketSfx *e)
{
    e->prev = NULL; e->next = head;
    if (head) head->prev = e; else tail = e;
    head = e;
}
static void evict(uint32_t need)
{
    PAL_PocketSfx *e = tail;
    while (e && used_bytes + need > PAL_POCKET_SFX_CACHE_BYTES) {
        PAL_PocketSfx *prev = e->prev;
        if (!e->refs) {
            unlink_entry(e);
            used_bytes -= e->bytes; --n_entries;
            free(e);
        }
        e = prev;
    }
}

PAL_PocketSfx *PAL_PocketSfxAcquire(int key, const void *data, uint32_t bytes,
                                    int bits, int channels, int rate, int out_rate)
{
    PAL_PocketSfx *e;
    uint32_t in_frames, out_frames, size, i;
    int16_t *src;
    for (e = head; e; e = e->next)
        if (e->key == key) {
            unlink_entry(e); push_front(e);
            ++e->refs; ++n_hits;
            return e;
        }
    if (!data || (bits != 8 && bits != 16) || channels < 1 || channels > 2 || rate <= 0) return NULL;
    in_frames = bytes / (uint32_t)(channels * bits / 8);
    if (!in_frames) return NULL;
    out_frames = (uint32_t)ppr_output_frames(in_frames, rate, out_rate);
    size = (uint32_t)sizeof(PAL_PocketSfx) + out_frames * (uint32_t)channels * 2u;
    PAL_PocketLog("SFX cache: key=%d %u frames %dbit %dch %dHz -> %u bytes (used %u, entries %u)", key, (unsigned)in_frames, bits, channels, rate, (unsigned)size, (unsigned)used_bytes, (unsigned)n_entries);
    evict(size);
    src = (int16_t *)malloc((size_t)in_frames * channels * sizeof(int16_t));
    e = (PAL_PocketSfx *)malloc(size);
    if (!src || !e) { free(src); free(e); return NULL; }
    if (bits == 8) {
        const uint8_t *u = (const uint8_t *)data;
        for (i = 0; i < in_frames * (uint32_t)channels; ++i) src[i] = (int16_t)(((int)u[i] - 128) << 8);
    } else {
        const uint8_t *b = (const uint8_t *)data;
        for (i = 0; i < in_frames * (uint32_t)channels; ++i) src[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
    }
    e->key = key; e->refs = 1; e->channels = channels; e->bytes = size;
    if (rate == out_rate) {
        memcpy(e->pcm, src, (size_t)in_frames * channels * sizeof(int16_t));
        e->frames = in_frames;
    } else {
        uint32_t done = 0;
        while (done < out_frames) {
            done += (uint32_t)ppr_convert_part(src, in_frames, channels, rate, out_rate,
                                               e->pcm, done, SLICE_FRAMES);
            PAL_PocketAudioDrain();
        }
        e->frames = out_frames;
    }
    free(src);
    push_front(e);
    used_bytes += size; ++n_entries; ++n_misses;
    return e;
}

void PAL_PocketSfxRelease(PAL_PocketSfx *sfx)
{
    if (!sfx || sfx->refs <= 0) return;
    --sfx->refs;
    if (!sfx->refs && used_bytes > PAL_POCKET_SFX_CACHE_BYTES) evict(0);
}

void PAL_PocketSfxStats(uint32_t *entries, uint32_t *bytes, uint32_t *hits, uint32_t *misses)
{
    if (entries) *entries = n_entries;
    if (bytes) *bytes = used_bytes;
    if (hits) *hits = n_hits;
    if (misses) *misses = n_misses;
}
