/* SPDX-License-Identifier: GPL-3.0-or-later
 * Test doubles for the SDK services called by the real platform adapter. */
#ifndef PAL_TEST_PLATFORM_OF_H
#define PAL_TEST_PLATFORM_OF_H
#include <stdint.h>
#include "of_input_types.h"
#define OF_CAPS_MAGIC 0x43415053u
#define OF_HW_FPU (1u << 8)
#define OF_DISPLAY_TERMINAL 0
#define OF_DISPLAY_FRAMEBUFFER 1
struct of_capabilities { uint32_t magic, cpu_freq_hz, sdram_size; };
typedef struct {
    uint16_t width, height, stride;
    uint8_t color_mode, reserved;
} of_video_mode_t;
const struct of_capabilities *of_get_caps(void);
int of_has_feature(uint32_t);
void of_file_set_idle_hook(void (*hook)(void));
void of_video_get_mode(of_video_mode_t *);
int of_video_set_mode(const of_video_mode_t *);
void of_video_set_display_mode(int);
void of_video_palette_bulk(const uint32_t *, int);
void of_input_poll_p0(void);
int of_btn(uint32_t);
#endif
