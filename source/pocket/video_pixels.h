/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PAL_POCKET_VIDEO_PIXELS_H
#define PAL_POCKET_VIDEO_PIXELS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The original game's pixel canvas. Display aspect belongs to video.json. */
#define PAL_POCKET_WIDTH 320
#define PAL_POCKET_HEIGHT 200
#define PAL_POCKET_COLORS 256

static inline uint32_t PAL_PocketRGB(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* Only visible pixels are copied; neither surface's pitch is assumed tight. */
static inline void PAL_PocketCopyRect(uint8_t *dst, size_t dp,
    const uint8_t *src, size_t sp, int x, int y, int w, int h)
{
    int row;
    int64_t right = (int64_t)x + w, bottom = (int64_t)y + h;
    if (!dst || !src || dp < PAL_POCKET_WIDTH || sp < PAL_POCKET_WIDTH ||
        w <= 0 || h <= 0) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > PAL_POCKET_WIDTH) right = PAL_POCKET_WIDTH;
    if (bottom > PAL_POCKET_HEIGHT) bottom = PAL_POCKET_HEIGHT;
    if (right <= x || bottom <= y) return;
    for (row = y; row < bottom; ++row)
        memcpy(dst + (size_t)row * dp + x,
               src + (size_t)row * sp + x, (size_t)(right - x));
}

/* SDLPAL's scripted shake is a native-row translation, never scaling. */
static inline void PAL_PocketComposeFrame(uint8_t *dst, size_t dp,
    const uint8_t *src, size_t sp, unsigned shake_time, unsigned shake_level)
{
    unsigned row;
    if (!shake_time || !shake_level) {
        PAL_PocketCopyRect(dst, dp, src, sp, 0, 0,
                          PAL_POCKET_WIDTH, PAL_POCKET_HEIGHT);
        return;
    }
    if (shake_level > PAL_POCKET_HEIGHT) shake_level = PAL_POCKET_HEIGHT;
    for (row = 0; row < PAL_POCKET_HEIGHT; ++row) {
        unsigned from = (shake_time & 1) ? row + shake_level : row - shake_level;
        if (from < PAL_POCKET_HEIGHT)
            memcpy(dst + row * dp, src + from * sp, PAL_POCKET_WIDTH);
        else
            memset(dst + row * dp, 0, PAL_POCKET_WIDTH);
    }
}

/* Preserve the original six-way dissolve and 16-color-bank fade. The lane
 * is based on visible pixel index, so padding never changes the animation. */
static inline void PAL_PocketTransitionStep(uint8_t *dst, size_t dp,
    const uint8_t *src, size_t sp, unsigned lane, int fade_pass)
{
    unsigned n;
    for (n = lane; n < PAL_POCKET_WIDTH * PAL_POCKET_HEIGHT; n += 6) {
        unsigned y = n / PAL_POCKET_WIDTH, x = n % PAL_POCKET_WIDTH;
        uint8_t a = src[y * sp + x], b = dst[y * dp + x];
        if (fade_pass < 0) b = a;
        else {
            if (fade_pass > 0) {
                if ((a & 15) > (b & 15)) ++b;
                else if ((a & 15) < (b & 15)) --b;
            }
            b = (a & 0xf0) | (b & 15);
        }
        dst[y * dp + x] = b;
    }
}
#endif
