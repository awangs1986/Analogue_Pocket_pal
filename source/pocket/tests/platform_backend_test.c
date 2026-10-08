/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "main.h"
#include "of.h"

SDL_Surface *gpScreen;
static SDL_Surface screen;
static SDL_Color game_colors[256], expected_colors[256];
static uint32_t hardware_colors[256];
static of_video_mode_t app_mode, visible_mode;
static int display_mode, palette_available, palette_uploads, palette_restores;
static int set_modes, renders, pumps, delays, polls, reject_restore;
static uint32_t button_sequence[8];
static unsigned button_count;

static uint32_t rgb(SDL_Color c)
{
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

SDL_Color *VIDEO_GetPalette(void) { return palette_available ? game_colors : NULL; }
void VIDEO_SetPalette(SDL_Color colors[256])
{
    unsigned i;
    assert(!memcmp(colors, expected_colors, sizeof(expected_colors)));
    for (i = 0; i < 256; ++i) hardware_colors[i] = rgb(colors[i]);
    memcpy(game_colors, colors, sizeof(game_colors));
    ++palette_restores;
}
void VIDEO_RenderCopy(void)
{
    assert(display_mode == OF_DISPLAY_FRAMEBUFFER);
    assert(visible_mode.width == 320 && visible_mode.height == 200);
    assert(visible_mode.stride == 336);
    ++renders;
}
void PAL_PocketAudioPump(void) { ++pumps; }
void __real_SDL_Delay(Uint32 ms) { assert(ms == 1); ++delays; }

void of_video_get_mode(of_video_mode_t *mode) { *mode = app_mode; }
int of_video_set_mode(const of_video_mode_t *mode)
{
    ++set_modes;
    assert(mode->width == 320 && mode->height == 200 && mode->stride == 336);
    if (reject_restore) return -1;
    app_mode = visible_mode = *mode;
    return 0;
}
void of_video_set_display_mode(int value)
{
    display_mode = value;
    if (value == OF_DISPLAY_TERMINAL)
        visible_mode = (of_video_mode_t){320, 240, 320, 0, 0};
    else {
        assert(value == OF_DISPLAY_FRAMEBUFFER);
        visible_mode = app_mode;
    }
}
void of_video_palette_bulk(const uint32_t *colors, int count)
{
    static const uint32_t terminal_colors[16] = {
        0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
        0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff
    };
    assert(display_mode == OF_DISPLAY_TERMINAL && count == 16);
    assert(!memcmp(colors, terminal_colors, sizeof(terminal_colors)));
    memcpy(hardware_colors, colors, sizeof(terminal_colors));
    ++palette_uploads;
}
void of_input_poll_p0(void)
{
    /* A dialog must already be readable before waiting for its first key. */
    assert(display_mode == OF_DISPLAY_TERMINAL);
    assert(visible_mode.width == 320 && visible_mode.height == 240);
    assert(hardware_colors[0] == 0 && hardware_colors[15] == 0xffffff);
    assert((unsigned)polls < button_count);
    ++polls;
}
int of_btn(uint32_t mask)
{
    assert(polls > 0 && mask == OF_BTN_A);
    return (button_sequence[polls - 1] & mask) != 0;
}

static void reset(int have_screen, int have_palette, int black_palette)
{
    unsigned i;
    gpScreen = have_screen ? &screen : NULL;
    palette_available = have_palette;
    palette_uploads = palette_restores = set_modes = renders = 0;
    pumps = delays = polls = reject_restore = 0;
    display_mode = have_screen ? OF_DISPLAY_FRAMEBUFFER : OF_DISPLAY_TERMINAL;
    app_mode = visible_mode = (of_video_mode_t){320, 200, 336, 0, 0};
    for (i = 0; i < 256; ++i) {
        game_colors[i] = black_palette ? (SDL_Color){0, 0, 0, 255} :
            (SDL_Color){i, 255 - i, i ^ 0x5a, 255};
        expected_colors[i] = game_colors[i];
        hardware_colors[i] = rgb(game_colors[i]);
    }
    /* An already held A must be released before a fresh A can dismiss. */
    button_sequence[0] = button_sequence[1] = OF_BTN_A;
    button_sequence[2] = button_sequence[3] = 0;
    button_sequence[4] = OF_BTN_A;
    button_count = 5;
}

int main(void)
{
    SDL_MessageBoxButtonData button = {0, 17, "OK"};
    SDL_MessageBoxData data = {SDL_MESSAGEBOX_ERROR, NULL, "Fixture alert",
        "Synthetic diagnostic", 1, &button, NULL};
    int id, black;
    unsigned i;

    for (black = 0; black <= 1; ++black) {
        reset(1, 1, black);
        id = -1;
        assert(SDL_ShowMessageBox(&data, &id) == 0 && id == 17);
        assert(palette_uploads == 1 && palette_restores == 1 && set_modes == 1 && renders == 1);
        assert(polls == 5 && delays == 4 && pumps == 4);
        for (i = 0; i < 256; ++i) assert(hardware_colors[i] == rgb(expected_colors[i]));
    }

    /* Startup errors can happen before video or its palette exists. */
    reset(0, 0, 1);
    assert(SDL_ShowMessageBox(&data, NULL) == 0);
    assert(palette_uploads == 1 && palette_restores == 0 && set_modes == 0 && renders == 0);
    assert(display_mode == OF_DISPLAY_TERMINAL && hardware_colors[15] == 0xffffff);

    reset(1, 0, 1);
    data.numbuttons = 0;
    data.buttons = NULL;
    id = -1;
    assert(SDL_ShowMessageBox(&data, &id) == 0 && id == 0);
    assert(palette_restores == 0 && set_modes == 0 && renders == 0);

    reset(1, 1, 1);
    assert(SDL_ShowMessageBox(NULL, NULL) == -1);
    assert(palette_uploads == 0 && set_modes == 0 && polls == 0);

    reset(1, 1, 1);
    reject_restore = 1;
    assert(SDL_ShowMessageBox(&data, NULL) == -1);
    assert(set_modes == 1 && palette_restores == 0 && renders == 0);
    assert(display_mode == OF_DISPLAY_TERMINAL && hardware_colors[15] == 0xffffff);
    puts("platform dialog contracts passed");
    return 0;
}
