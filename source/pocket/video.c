/* SPDX-License-Identifier: GPL-3.0-only
 * SDLPAL openfpgaOS video backend. Transitions follow sdlpal/video.c,
 * Copyright (c) 2009-2011 Wei Mingzhi and 2011-2026 SDLPAL contributors.
 */
#include "main.h"
#include "pocket_log.h"
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
/* AVI (PAL98 1-6.avi, 288x180 RGB555) temporarily owns the display.
 * Preferred: switch the openfpgaOS framebuffer to 320x200 RGB555 (the
 * scanout supports direct color; same 320x200 scaler slot). Fallback when
 * the runtime refuses that mode: stay 8-bit and quantize each frame to a
 * fixed 3-3-2 palette with 4x4 ordered dithering. */
enum { AVI_OFF, AVI_RGB555, AVI_DITHER332 };
static int g_avi_mode = AVI_OFF;

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
    if (!g_ready || !g_palette || g_avi_mode == AVI_DITHER332) return;
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
    PAL_PocketLog("VIDEO: set 320x200 8bit");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0 ||
        of_video_check_mode(&requested, NULL) != 0 ||
        of_video_set_mode(&requested) != 0 || !native_mode(&actual)) {
        UTIL_LogOutput(LOGLEVEL_ERROR, "Pocket requires native 320x200 indexed video; runtime mode unavailable.\n");
        return -1;
    }
    PAL_PocketLog("VIDEO: mode %ux%u stride %u color %u", actual.width, actual.height, actual.stride, actual.color_mode);
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

void PAL_PocketVideoRestorePalette(void) { upload_palette(); VIDEO_RenderCopy(); }
VOID VIDEO_RenderCopy(VOID)
{
    of_video_mode_t mode;
    uint8_t *frame;
    PAL_PocketAudioPump();
    PAL_PocketLogHeartbeat("render");
    if (!g_ready || g_bRenderPaused || g_avi_mode != AVI_OFF) return;
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

/* aviplay.c's MS Video1 decoder always stores RGB555 words with a row
 * stride of pitch/2 pixels. The SDK SDL shim tags any non-8-bit surface as
 * 32-bit RGB888 (twice the pitch needed), so only the geometry is checked. */
static int rgb555_surface(const SDL_Surface *s)
{
    return s && s->pixels && s->format && s->format->BitsPerPixel > 8 &&
        s->w > 0 && s->h > 0 && s->w <= PAL_POCKET_WIDTH && s->h <= PAL_POCKET_HEIGHT &&
        s->pitch >= s->w * 2;
}

static void draw_avi_frame(const SDL_Surface *surface)
{
    of_video_mode_t mode;
    uint8_t *frame;
    PAL_PocketAudioPump();
    if (!g_ready || g_bRenderPaused) return;
    of_video_get_mode(&mode);
    frame = of_video_surface();
    if (!frame) return;
    if (g_avi_mode == AVI_RGB555) {
        if (mode.color_mode != OF_VIDEO_MODE_RGB555 || mode.width != PAL_POCKET_WIDTH ||
            mode.height != PAL_POCKET_HEIGHT) return;
        PAL_PocketBlitRGB555(frame, mode.stride, surface->pixels, (size_t)(surface->pitch / 2) * 2,
                             surface->w, surface->h);
    } else {
        if (!native_mode(&mode)) return;
        PAL_PocketBlitDither332(frame, mode.stride, surface->pixels, surface->pitch,
                                surface->w, surface->h);
    }
    of_video_flush();
    of_video_flip();
    PAL_PocketAudioPump();
}

VOID VIDEO_DrawSurfaceToScreen(SDL_Surface *surface)
{
    if (g_avi_mode != AVI_OFF && rgb555_surface(surface)) {
        draw_avi_frame(surface);
        return;
    }
    if (!native_surface(surface)) {
        UTIL_LogOutput(LOGLEVEL_ERROR, "Pocket: unsupported surface for display (%d bpp %dx%d).\n",
            surface && surface->format ? surface->format->BitsPerPixel : 0,
            surface ? surface->w : 0, surface ? surface->h : 0);
        return;
    }
    draw_native(surface, 0);
}

/* These desktop controls cannot change the native canvas. */
VOID VIDEO_Resize(INT w, INT h) { (void)w; (void)h; }
VOID VIDEO_ToggleFullscreen(VOID) {}
static int set_native_mode(void)
{
    of_video_mode_t requested = {
        PAL_POCKET_WIDTH, PAL_POCKET_HEIGHT, PAL_POCKET_WIDTH, OF_VIDEO_MODE_8BIT, 0
    };
    of_video_mode_t actual;
    return of_video_set_mode(&requested) == 0 && native_mode(&actual) ? 0 : -1;
}

/* aviplay.c: surface depth (>8) before the first frame, 0 when it ends. */
VOID VIDEO_ChangeDepth(INT bpp)
{
    if (!g_ready) return;
    if (bpp > 8) {
        of_video_mode_t requested = {
            PAL_POCKET_WIDTH, PAL_POCKET_HEIGHT, PAL_POCKET_WIDTH * 2, OF_VIDEO_MODE_RGB555, 0
        };
        of_video_mode_t actual;
        if (g_avi_mode != AVI_OFF) return;
#ifndef PAL_POCKET_AVI_FORCE_DITHER
        if (of_video_check_mode(&requested, NULL) == 0 && of_video_set_mode(&requested) == 0) {
            of_video_get_mode(&actual);
            if (actual.color_mode == OF_VIDEO_MODE_RGB555 && actual.width == PAL_POCKET_WIDTH &&
                actual.height == PAL_POCKET_HEIGHT && actual.stride >= PAL_POCKET_WIDTH * 2) {
                PAL_PocketLog("VIDEO: AVI RGB555 mode");
                g_avi_mode = AVI_RGB555;
                UTIL_LogOutput(LOGLEVEL_INFO, "Pocket AVI: 320x200 RGB555 framebuffer\n");
                return;
            }
        }
#endif
        /* Restore the indexed mode and dither instead. */
        if (set_native_mode() != 0) {
            UTIL_LogOutput(LOGLEVEL_ERROR, "Pocket AVI: cannot restore indexed mode\n");
            return;
        }
        {
            uint32_t colors[PAL_POCKET_COLORS];
            PAL_PocketPalette332(colors);
            of_video_palette_bulk(colors, PAL_POCKET_COLORS);
        }
        PAL_PocketLog("VIDEO: AVI dither332 mode");
        g_avi_mode = AVI_DITHER332;
        UTIL_LogOutput(LOGLEVEL_WARNING, "Pocket AVI: RGB555 unavailable, using 3-3-2 dithered palette\n");
        return;
    }
    if (bpp == 0 || bpp == 8) {
        if (g_avi_mode == AVI_OFF) return;
        g_avi_mode = AVI_OFF;
        if (set_native_mode() != 0)
            UTIL_LogOutput(LOGLEVEL_ERROR, "Pocket AVI: cannot restore 320x200 indexed mode\n");
        upload_palette();
        VIDEO_RenderCopy();
        return;
    }
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
