/* SPDX-License-Identifier: GPL-3.0-only
 * Test double for only the public SDK calls used by pocket/video.c. */
#ifndef TEST_OF_VIDEO_H
#define TEST_OF_VIDEO_H
#include <stdint.h>
#define OF_VIDEO_MODE_8BIT 0
#define OF_DISPLAY_FRAMEBUFFER 1
typedef struct {
    uint16_t width, height, stride;
    uint8_t color_mode, reserved;
} of_video_mode_t;
int of_video_check_mode(const of_video_mode_t *, of_video_mode_t *);
int of_video_set_mode(const of_video_mode_t *);
void of_video_get_mode(of_video_mode_t *);
uint8_t *of_video_surface(void);
void of_video_palette_bulk(const uint32_t *, int);
void of_video_set_display_mode(int);
void of_video_flush(void);
void of_video_flip(void);
#endif
