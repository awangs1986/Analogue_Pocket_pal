/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright-free on-device probe. Never writes a save or config file.
 * Pattern: all native edges, one-pixel checkerboard, corrected 4:3 circle.
 */
#include "of.h"
#include "files.h"
#include "audio_stream.h"
#include "video_pixels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static ppa_stream *music;
static int16_t pending[512];
static int pending_count, pending_offset, audio_capacity;
static unsigned starvation, started, starved;

static void drain(void)
{
    int pass;
    for (pass = 0; pass < 8; ++pass) {
        int free_frames = of_audio_free();
        int queued = audio_capacity - free_frames;
        int room = 2048 - queued;
        if (started && queued <= 4) { if (!starved) ++starvation; starved = 1; }
        else starved = 0;
        if (room > free_frames) room = free_frames;
        if (room <= 0) return;
        if (!pending_count) {
            if (room > 256) room = 256;
            pending_count = (int)ppa_read(music, pending, (size_t)room);
            pending_offset = 0;
            if (!pending_count) return;
        }
        if (room > pending_count) room = pending_count;
        int n = of_audio_write(pending + 2 * pending_offset, room);
        if (n <= 0) return;
        pending_count -= n; pending_offset += n; started = 1;
    }
}
static void draw(unsigned frame)
{
    of_video_mode_t mode;
    of_video_get_mode(&mode);
    uint8_t *fb = of_video_surface();
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 320; ++x) {
            int dx = x - 160, dy = y - 100;
            unsigned color = 1 + x / 22;
            if (x < 64 && y < 64) color = ((x ^ y) & 1) ? 15 : 0;
            if (dx*dx*2500 + dy*dy*3600 < 9000000) color = 14;
            if (y == 0 || y == 199 || x == 0 || x == 319) color = 15;
            if (y >= 180 && y < 190 && x == (int)(frame % 318) + 1) color = 0;
            fb[y * mode.stride + x] = (uint8_t)color;
        }
    }
    of_video_flush(); of_video_flip();
}
int main(void)
{
    of_video_mode_t requested = {320, 200, 320, OF_VIDEO_MODE_8BIT, 0}, actual;
    uint32_t colors[256] = {0};
    uint32_t last = of_time_us(), next_frame = last, decode_max = 0, gap_max = 0;
    uint32_t report = last, frame = 0;
    uint64_t decode_total = 0;
    int overlay = 0, old_start = 0;
    printf("PAL synthetic probe: no game files and no save writes.\n");
    if (PAL_PocketFilesInit() != 0) { printf("%s\n", PAL_PocketFileError()); return 1; }
    music = ppa_create();
    if (!music || ppa_start(music, PAL_PocketOpenAsset("ogg/001.ogg"), 1) != 0) {
        printf("Cannot open synthetic OGG fixture.\n"); return 2;
    }
    of_video_init();
    if (of_video_set_mode(&requested)) return 3;
    of_video_get_mode(&actual);
    if (actual.width != 320 || actual.height != 200 || actual.stride < 320 ||
        actual.color_mode != OF_VIDEO_MODE_8BIT) return 4;
    for (unsigned i = 1; i < 16; ++i)
        colors[i] = PAL_PocketRGB((i & 1) ? 255 : 0, (i & 2) ? 255 : 0, (i & 4) ? 255 : 0);
    colors[14] = 0x808080; colors[15] = 0xffffff;
    of_video_palette_bulk(colors, 256);
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
    of_audio_init(); audio_capacity = of_audio_free();
    if (audio_capacity <= 1) return 5;
    of_file_set_idle_hook(drain);
    for (;;) {
        uint32_t now = of_time_us(), gap = now - last;
        if (gap > gap_max) gap_max = gap;
        last = now;
        drain();
        now = of_time_us();
        ppa_pump(music, 4096, 64);
        uint32_t elapsed = of_time_us() - now;
        decode_total += elapsed;
        if (elapsed > decode_max) decode_max = elapsed;
        drain();
        if (ppa_status(music) == PPA_ERROR) {
            of_video_set_display_mode(OF_DISPLAY_TERMINAL);
            printf("OGG decode error: %s\n", ppa_error(music));
            break;
        }
        of_input_poll_p0();
        int start = of_btn(OF_BTN_START);
        if (start && !old_start) {
            overlay = !overlay;
            of_video_set_display_mode(overlay ? OF_DISPLAY_OVERLAY : OF_DISPLAY_FRAMEBUFFER);
        }
        old_start = start;
        now = of_time_us();
        if ((int32_t)(now - next_frame) >= 0) { draw(frame++); next_frame = now + 33333u; }
        if (now - report >= 5000000u) {
            const ppa_stats *s = ppa_get_stats(music);
            printf("\033[2J\033[H320x200 / 4:3 / OGG probe\nStart: status on/off\n"
                   "CPU Hz: %lu\nOGG: %u Hz / %u ch\nDecode max: %lu us\n"
                   "Pump gap: %lu us\nDecode budget: %lu / 1000\n"
                   "Starves: %u  loops: %lu\nAll 4 white edges must be visible.\nCircle must look round.\n",
                   (unsigned long)of_get_caps()->cpu_freq_hz, s->source_rate, s->source_channels,
                   (unsigned long)decode_max, (unsigned long)gap_max,
                   (unsigned long)(decode_total * 1000 / (now - report)),
                   starvation, (unsigned long)s->loops);
            decode_total = 0; report = now;
        }
        usleep(1000);
    }
    of_file_set_idle_hook(NULL); of_audio_init();
    ppa_destroy(music); PAL_PocketFilesShutdown();
    return 6;
}
