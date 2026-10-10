/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PAL_POCKET_SFX_CACHE_H
#define PAL_POCKET_SFX_CACHE_H
#include <stdint.h>

/* Sound effects are converted once, when first played, to 48 kHz signed
 * 16-bit with the fixed-point polyphase FIR (pocket_resample.c) and kept in
 * a size-bounded LRU cache.  Mixing a cached effect is a plain add. */
#define PAL_POCKET_SFX_CACHE_BYTES (8u << 20)

typedef struct PAL_PocketSfx {
    struct PAL_PocketSfx *prev, *next;
    int key;
    int refs;
    int channels;
    uint32_t frames;
    uint32_t bytes;
    int16_t pcm[];
} PAL_PocketSfx;

/* bits = 8 (unsigned) or 16 (signed little-endian). Returns a referenced
 * entry (call PAL_PocketSfxRelease when the voice ends) or NULL. */
PAL_PocketSfx *PAL_PocketSfxAcquire(int key, const void *data, uint32_t bytes,
                                    int bits, int channels, int rate, int out_rate);
void PAL_PocketSfxRelease(PAL_PocketSfx *sfx);
/* Continue converting a first-played effect in the background. */
void PAL_PocketSfxPump(void);
void PAL_PocketSfxStats(uint32_t *entries, uint32_t *bytes, uint32_t *hits, uint32_t *misses);
#endif
