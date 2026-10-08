/* SPDX-License-Identifier: GPL-3.0-only
 * SDLPAL openfpgaOS video backend. Transitions follow sdlpal/video.c,
 * Copyright (c) 2009-2011 Wei Mingzhi and 2011-2026 SDLPAL contributors.
 */
#include "main.h"
#include "of_video.h"
#include "video_pixels.h"

extern void PAL_PocketAudioPump(void);

SDL_Surface *gpScreen;
SDL_Surface *gpScreenBak;
SDL_Surface *gpScreenReal;
volatile BOOL g_bRenderPaused = FALSE;
static SDL_Palette *g_palette;
static WORD g_shake_time, g_shake_level;
static int g_ready;

static int native_surface(const SDL_Surface *surface)
{
    return surface && surface->pixels && surface->format &&
        surface->format->BitsPerPixel == 8 &&
        surface->w == PAL_POCKET_WIDTH && surface->h == PAL_POCKET_HEIGHT &&
        surface->pitch >= PAL_POCKET_WIDTH;
}

static int native_mode(of_video_mode_t *mode)
{
    of_video_get_mode(mode);
    return mode->width == PAL_POCKET_WIDTH && mode->height == PAL_POCKET_HEIGHT &&
        mode->color_mode == OF_VIDEO_MODE_8BIT && mode->stride >= PAL_POCKET_WIDTH;
}

static void upload_palette(void)
{
    uint32_t colors[PAL_POCKET_COLORS];
    unsigned i;
    if (!g_ready || !g_palette) return;
    for (i = 0; i < PAL_POCKET_COLORS; ++i) {
        SDL_Color c = g_palette->colors[i];
        colors[i] = PAL_PocketRGB(c.r, c.g, c.b);
    }
    of_video_palette_bulk(colors, PAL_POCKET_COLORS);
}

INT VIDEO_Startup(VOID)
{
    of_video_mode_t requested = {
        PAL_POCKET_WIDTH, PAL_POCKET_HEIGHT, PAL_POCKET_WIDTH, OF_VIDEO_MODE_8BIT, 0
    };
    of_video_mode_t actual;
    if (g_ready) return 0;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0 ||
        of_video_check_mode(&requested, NULL) != 0 ||
        of_video_set_mode(&requested) != 0 || !native_mode(&actual)) {
        UTIL_LogOutput(LOGLEVEL_ERROR, "Pocket requires native 320x200 indexed video; runtime mode unavailable.\n");
        return -1;
    }
    gpScreen = SDL_CreateRGBSurface(SDL_SWSURFACE, PAL_POCKET_WIDTH, PAL_POCKET_HEIGHT, 8, 0, 0, 0, 0);
    gpScreenBak = SDL_CreateRGBSurface(SDL_SWSURFACE, PAL_POCKET_WIDTH, PAL_POCKET_HEIGHT, 8, 0, 0, 0, 0);
    gpScreenReal = SDL_CreateRGBSurface(SDL_SWSURFACE, PAL_POCKET_WIDTH, PAL_POCKET_HEIGHT, 8, 0, 0, 0, 0);
    g_palette = SDL_AllocPalette(PAL_POCKET_COLORS);
    if (!native_surface(gpScreen) || !native_surface(gpScreenBak) ||
        !native_surface(gpScreenReal) || !g_palette || !g_palette->colors) {
        VIDEO_Shutdown();
        return -2;
    }
    VIDEO_UpdateSurfacePalette(gpScreen);
    VIDEO_UpdateSurfacePalette(gpScreenBak);
    VIDEO_UpdateSurfacePalette(gpScreenReal);
    SDL_FillRect(gpScreen, NULL, 0);
    SDL_FillRect(gpScreenBak, NULL, 0);
    SDL_FillRect(gpScreenReal, NULL, 0);
    g_shake_time = g_shake_level = 0;
    g_bRenderPaused = FALSE;
    g_ready = 1;
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
    upload_palette();
    VIDEO_RenderCopy();
    return 0;
}

VOID VIDEO_Shutdown(VOID)
{
    g_ready = 0;
    SDL_FreeSurface(gpScreen);
    SDL_FreeSurface(gpScreenBak);
    SDL_FreeSurface(gpScreenReal);
    gpScreen = gpScreenBak = gpScreenReal = NULL;
    SDL_FreePalette(g_palette);
    g_palette = NULL;
    g_shake_time = g_shake_level = 0;
}

VOID VIDEO_RenderCopy(VOID)
{
    of_video_mode_t mode;
    uint8_t *frame;
    PAL_PocketAudioPump();
    if (!g_ready || g_bRenderPaused) return;
    if (!native_mode(&mode)) {
        UTIL_LogOutput(LOGLEVEL_ERROR, "Pocket video mode changed unexpectedly; refusing to scale the game canvas.\n");
        return;
    }
    frame = of_video_surface();
    if (!frame) return;
    /* Every draw buffer gets a full copy. Reused triple-buffer pages must not
     * expose stale pixels after a dirty-rectangle update. */
    PAL_PocketCopyRect(frame, mode.stride, gpScreenReal->pixels, gpScreenReal->pitch,
                      0, 0, PAL_POCKET_WIDTH, PAL_POCKET_HEIGHT);
    of_video_flush();
    of_video_flip();
    PAL_PocketAudioPump();
}

static void draw_native(SDL_Surface *source, int shake)
{
    if (!g_ready || g_bRenderPaused) return;
    PAL_PocketComposeFrame(gpScreenReal->pixels, gpScreenReal->pitch,
        source->pixels, source->pitch, shake ? g_shake_time : 0, g_shake_level);
    if (shake && g_shake_time) --g_shake_time;
    VIDEO_RenderCopy();
}

VOID VIDEO_UpdateScreen(const SDL_Rect *rect)
{
    PAL_PocketAudioPump();
    if (!g_ready || g_bRenderPaused) return;
    if (!rect) draw_native(gpScreen, 1);
    else {
        PAL_PocketCopyRect(gpScreenReal->pixels, gpScreenReal->pitch,
            gpScreen->pixels, gpScreen->pitch, rect->x, rect->y, rect->w, rect->h);
        VIDEO_RenderCopy();
    }
}

VOID VIDEO_SetPalette(SDL_Color colors[256])
{
    if (!g_palette || !colors) return;
    SDL_SetPaletteColors(g_palette, colors, 0, PAL_POCKET_COLORS);
    /* Palette-only fades must be visible without another pixel update. */
    upload_palette();
    PAL_PocketAudioPump();
}

SDL_Color *VIDEO_GetPalette(VOID)
{
    return g_palette ? g_palette->colors : NULL;
}

VOID VIDEO_ShakeScreen(WORD time, WORD level)
{
    g_shake_time = time;
    g_shake_level = level > PAL_POCKET_HEIGHT ? PAL_POCKET_HEIGHT : level;
}

VOID VIDEO_SwitchScreen(WORD speed)
{
    static const unsigned lanes[6] = {0, 3, 1, 5, 2, 4};
    unsigned i;
    if (!g_ready) return;
    for (i = 0; i < 6; ++i) {
        PAL_PocketTransitionStep(gpScreenBak->pixels, gpScreenBak->pitch,
            gpScreen->pixels, gpScreen->pitch, lanes[i], -1);
        draw_native(gpScreenBak, 0);
        UTIL_Delay(((uint32_t)speed + 1) * 10);
    }
}

VOID VIDEO_FadeScreen(WORD speed)
{
    static const unsigned lanes[6] = {0, 3, 1, 5, 2, 4};
    unsigned pass, lane;
    uint32_t next = SDL_GetTicks(), interval = ((uint32_t)speed + 1) * 10;
    if (!g_ready) return;
    for (pass = 0; pass < 12; ++pass) {
        for (lane = 0; lane < 6; ++lane) {
            PAL_ProcessEvent();
            while (!SDL_TICKS_PASSED(SDL_GetTicks(), next)) {
                PAL_ProcessEvent();
                SDL_Delay(5);
            }
            next = SDL_GetTicks() + interval;
            PAL_PocketTransitionStep(gpScreenBak->pixels, gpScreenBak->pitch,
                gpScreen->pixels, gpScreen->pitch, lanes[lane], (int)pass);
            draw_native(gpScreenBak, 1);
        }
    }
    VIDEO_UpdateScreen(NULL);
}

VOID VIDEO_UpdateSurfacePalette(SDL_Surface *surface)
{
    /* The SDK increments refcount even when rebinding the same palette. */
    if (surface && surface->format && surface->format->BitsPerPixel == 8 &&
        g_palette && surface->format->palette != g_palette)
        SDL_SetSurfacePalette(surface, g_palette);
}

SDL_Surface *VIDEO_CreateCompatibleSizedSurface(SDL_Surface *source, const SDL_Rect *size)
{
    SDL_Surface *result;
    if (!source || !source->format) return NULL;
    result = SDL_CreateRGBSurface(SDL_SWSURFACE,
        size ? size->w : source->w, size ? size->h : source->h,
        source->format->BitsPerPixel, source->format->Rmask,
        source->format->Gmask, source->format->Bmask, source->format->Amask);
    VIDEO_UpdateSurfacePalette(result);
    return result;
}

SDL_Surface *VIDEO_CreateCompatibleSurface(SDL_Surface *source)
{
    return VIDEO_CreateCompatibleSizedSurface(source, NULL);
}

SDL_Surface *VIDEO_DuplicateSurface(SDL_Surface *source, const SDL_Rect *rect)
{
    SDL_Surface *result = VIDEO_CreateCompatibleSizedSurface(source, rect);
    if (result && SDL_BlitSurface(source, rect, result, NULL) != 0) {
        SDL_FreeSurface(result);
        return NULL;
    }
    return result;
}

VOID VIDEO_DrawSurfaceToScreen(SDL_Surface *surface)
{
    if (!native_surface(surface)) {
        UTIL_LogOutput(LOGLEVEL_ERROR, "Pocket supports only 320x200 indexed frames; truecolor AVI is disabled.\n");
        return;
    }
    draw_native(surface, 0);
}

/* These desktop controls cannot change the native canvas. */
VOID VIDEO_Resize(INT w, INT h) { (void)w; (void)h; }
VOID VIDEO_ToggleFullscreen(VOID) {}
VOID VIDEO_ChangeDepth(INT bpp)
{
    if (bpp != 0 && bpp != 8)
        UTIL_LogOutput(LOGLEVEL_WARNING, "Pocket output stays 8-bit indexed (requested %d bpp).\n", bpp);
}
VOID VIDEO_SaveScreenshot(VOID)
{
    UTIL_LogOutput(LOGLEVEL_WARNING, "Pocket screenshots are not implemented in this port.\n");
}
void VIDEO_SetWindowTitle(const char *title) { (void)title; }
VOID VIDEO_SetupTouchArea(int window_w, int window_h, int draw_w, int draw_h)
{
    (void)window_w; (void)window_h; (void)draw_w; (void)draw_h;
}
