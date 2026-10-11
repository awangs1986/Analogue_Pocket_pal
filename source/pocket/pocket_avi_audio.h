/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PAL_POCKET_AVI_AUDIO_H
#define PAL_POCKET_AVI_AUDIO_H
#include <stdint.h>
/* Called by sdlpal/aviplay.c on the Pocket target (pocket/audio.c). */
int PAL_PocketAVIAudioBegin(int rate, int channels, int bits);
void PAL_PocketAVIAudioFeed(const void *data, uint32_t bytes);
void PAL_PocketAVIAudioEnd(void);
uint32_t PAL_PocketAVIAudioQueued(void);
uint32_t PAL_PocketTimeUs(void);
#endif
