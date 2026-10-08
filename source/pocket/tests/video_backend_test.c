/* SPDX-License-Identifier: GPL-3.0-only */
#include "main.h"
#include "of_video.h"
#include "video_pixels.h"

extern SDL_Surface *gpScreenReal;
static of_video_mode_t mode;
static uint8_t pages[3][PAL_POCKET_HEIGHT][336];
static uint32_t palette[256];
static int draw_page, shown_page, flips, flushes, audio_pumps, logs;
static int surface_count, palette_count, reject_mode, allocations, fail_allocation;
static uint32_t ticks;

int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
Uint32 SDL_GetTicks(void) { return ticks; }
void SDL_Delay(Uint32 time) { ticks += time; }
void UTIL_Delay(DWORD time) { ticks += time; }
void PAL_ProcessEvent(void) {}
void PAL_PocketAudioPump(void) { ++audio_pumps; }
void UTIL_LogOutput(LOGLEVEL level, const char *format, ...) { (void)level; (void)format; ++logs; }

SDL_Palette *SDL_AllocPalette(int count)
{
    SDL_Palette *p = calloc(1, sizeof(*p));
    assert(p);
    p->ncolors = count;
    p->colors = calloc((size_t)count, sizeof(*p->colors));
    p->refcount = 1;
    ++palette_count;
    return p;
}
void SDL_FreePalette(SDL_Palette *p)
{
    if (p && --p->refcount == 0) { free(p->colors); free(p); --palette_count; }
}
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
    Uint32 r, Uint32 g, Uint32 b, Uint32 a)
{
    SDL_Surface *s;
    ++allocations;
    if (allocations == fail_allocation) return NULL;
    s = calloc(1, sizeof(*s));
    assert(s);
    s->format = calloc(1, sizeof(*s->format));
    s->format->BitsPerPixel = depth;
    s->format->BytesPerPixel = (depth + 7) / 8;
    s->format->Rmask = r; s->format->Gmask = g; s->format->Bmask = b; s->format->Amask = a;
    s->format->palette = depth == 8 ? SDL_AllocPalette(256) : NULL;
    s->flags = flags;
    s->w = w; s->h = h;
    s->pitch = w * s->format->BytesPerPixel + 8;
    s->pixels = malloc((size_t)s->pitch * h);
    memset(s->pixels, 0xcd, (size_t)s->pitch * h);
    ++surface_count;
    return s;
}
void SDL_FreeSurface(SDL_Surface *s)
{
    if (!s) return;
    SDL_FreePalette(s->format->palette);
    free(s->format); free(s->pixels); free(s);
    --surface_count;
}
int SDL_SetSurfacePalette(SDL_Surface *s, SDL_Palette *p)
{
    if (s->format->palette != p) SDL_FreePalette(s->format->palette);
    s->format->palette = p;
    ++p->refcount; /* Deliberately reproduce the SDK's same-palette behavior. */
    return 0;
}
int SDL_SetPaletteColors(SDL_Palette *p, const SDL_Color *c, int first, int count)
{
    memcpy(p->colors + first, c, sizeof(*c) * count); ++p->version; return 0;
}
int SDL_FillRect(SDL_Surface *s, const SDL_Rect *r, Uint32 color)
{
    int y;
    SDL_Rect whole = {0, 0, s->w, s->h};
    if (!r) r = &whole;
    for (y = r->y; y < r->y + r->h; ++y)
        memset((uint8_t *)s->pixels + (size_t)y * s->pitch + r->x, color, r->w);
    return 0;
}
int SDL_UpperBlit(SDL_Surface *s, const SDL_Rect *sr, SDL_Surface *d, SDL_Rect *dr)
{
    int y;
    SDL_Rect all = {0, 0, s->w, s->h}, dest = {0, 0, d->w, d->h};
    if (!sr) sr = &all;
    if (!dr) dr = &dest;
    for (y = 0; y < sr->h; ++y)
        memcpy((uint8_t *)d->pixels + (size_t)(dr->y + y) * d->pitch + dr->x,
               (uint8_t *)s->pixels + (size_t)(sr->y + y) * s->pitch + sr->x, sr->w);
    return 0;
}
int of_video_check_mode(const of_video_mode_t *m, of_video_mode_t *out)
{
    assert(m->width == 320 && m->height == 200 && m->color_mode == 0);
    if (out) *out = *m;
    return reject_mode ? -1 : 0;
}
int of_video_set_mode(const of_video_mode_t *m) { mode = *m; mode.stride = 336; return 0; }
void of_video_get_mode(of_video_mode_t *out) { *out = mode; }
uint8_t *of_video_surface(void) { return &pages[draw_page][0][0]; }
void of_video_palette_bulk(const uint32_t *colors, int count)
{
    assert(count == 256); memcpy(palette, colors, sizeof(palette));
}
void of_video_set_display_mode(int value) { assert(value == OF_DISPLAY_FRAMEBUFFER); }
void of_video_flush(void) { ++flushes; }
void of_video_flip(void) { ++flips; shown_page = draw_page; draw_page = (draw_page + 1) % 3; }

static uint8_t pixel(int x, int y) { return (uint8_t)(x * 13 + y * 31); }
static void check_guards(void)
{
    int page, y, x;
    for (page = 0; page < 3; ++page)
        for (y = 0; y < 200; ++y)
            for (x = 320; x < 336; ++x) assert(pages[page][y][x] == 0xa5);
    for (y = 0; y < 200; ++y)
        for (x = 320; x < gpScreen->pitch; ++x)
            assert(((uint8_t *)gpScreen->pixels)[y * gpScreen->pitch + x] == 0xcd);
}

int main(void)
{
    int x, y, i, before;
    SDL_Color colors[256];
    SDL_Surface *copy, *bad;
    SDL_Rect area = {15, 21, 31, 17};
    memset(pages, 0xa5, sizeof(pages));
    reject_mode = 1;
    assert(VIDEO_Startup() == -1 && surface_count == 0);
    reject_mode = 0;
    fail_allocation = 2;
    assert(VIDEO_Startup() == -2 && surface_count == 0 && palette_count == 0);
    fail_allocation = 0;
    assert(VIDEO_Startup() == 0);
    assert(gpScreen->w == 320 && gpScreen->h == 200 && gpScreen->format->BitsPerPixel == 8);
    assert(VIDEO_Startup() == 0 && surface_count == 3);
    for (i = 0; i < 256; ++i) {
        colors[i].r = i; colors[i].g = 255 - i; colors[i].b = i ^ 0x5a; colors[i].a = 255;
    }
    VIDEO_SetPalette(colors);
    for (i = 0; i < 256; ++i) {
        assert(palette[i] == PAL_PocketRGB(i, 255 - i, i ^ 0x5a));
        assert(memcmp(&VIDEO_GetPalette()[i], &colors[i], sizeof(colors[i])) == 0);
    }
    assert(gpScreen->format->palette == gpScreenBak->format->palette);
    for (i = 0; i < 100; ++i) VIDEO_UpdateSurfacePalette(gpScreen);
    assert(gpScreen->format->palette->refcount == 4);
    for (y = 0; y < 200; ++y)
        for (x = 0; x < 320; ++x) ((uint8_t *)gpScreen->pixels)[y * gpScreen->pitch + x] = pixel(x, y);
    for (i = 0; i < 4; ++i) {
        VIDEO_UpdateScreen(NULL);
        for (y = 0; y < 200; ++y)
            for (x = 0; x < 320; ++x) assert(pages[shown_page][y][x] == pixel(x, y));
    }
    SDL_FillRect(gpScreen, &area, 99);
    for (i = 0; i < 4; ++i) {
        VIDEO_UpdateScreen(&area);
        for (y = 0; y < 200; ++y)
            for (x = 0; x < 320; ++x)
                assert(pages[shown_page][y][x] ==
                    ((x >= 15 && x < 46 && y >= 21 && y < 38) ? 99 : pixel(x, y)));
    }
    area.x = 0; area.y = 0; area.w = 320; area.h = 200;
    copy = VIDEO_DuplicateSurface(gpScreen, &area);
    assert(copy && copy->format->palette == gpScreen->format->palette);
    for (y = 0; y < 200; ++y)
        assert(memcmp((uint8_t *)copy->pixels + y * copy->pitch,
                      (uint8_t *)gpScreen->pixels + y * gpScreen->pitch, 320) == 0);
    VIDEO_UpdateSurfacePalette(copy);
    SDL_FreeSurface(copy);
    assert(gpScreen->format->palette->refcount == 4);
    VIDEO_ShakeScreen(2, 3);
    VIDEO_UpdateScreen(NULL);
    for (x = 0; x < 320; ++x) assert(pages[shown_page][0][x] == 0);
    assert(pages[shown_page][3][0] == pixel(0, 0));
    VIDEO_UpdateScreen(NULL);
    for (x = 0; x < 320; ++x) assert(pages[shown_page][199][x] == 0);
    assert(pages[shown_page][0][0] == pixel(0, 3));
    VIDEO_ShakeScreen(1, 65535);
    VIDEO_UpdateScreen(NULL);
    for (y = 0; y < 200; ++y)
        for (x = 0; x < 320; ++x) assert(pages[shown_page][y][x] == 0);
    before = flips;
    g_bRenderPaused = TRUE;
    VIDEO_UpdateScreen(NULL);
    VIDEO_RenderCopy();
    assert(flips == before);
    g_bRenderPaused = FALSE;
    bad = SDL_CreateRGBSurface(0, 320, 200, 16, 0, 0, 0, 0);
    VIDEO_DrawSurfaceToScreen(bad);
    assert(flips == before);
    SDL_FreeSurface(bad);
    VIDEO_SwitchScreen(0);
    assert(flips == before + 6);
    for (y = 0; y < 200; ++y)
        assert(memcmp((uint8_t *)gpScreenBak->pixels + y * gpScreenBak->pitch,
                      (uint8_t *)gpScreen->pixels + y * gpScreen->pitch, 320) == 0);
    SDL_FillRect(gpScreenBak, NULL, 0);
    VIDEO_FadeScreen(0);
    assert(flips == before + 6 + 73);
    for (y = 0; y < 200; ++y) {
        for (x = 0; x < 320; ++x) {
            uint8_t source = ((uint8_t *)gpScreen->pixels)[y * gpScreen->pitch + x];
            uint8_t faded = (source & 0xf0) | ((source & 15) < 11 ? (source & 15) : 11);
            assert(((uint8_t *)gpScreenBak->pixels)[y * gpScreenBak->pitch + x] == faded);
            assert(pages[shown_page][y][x] == source);
        }
    }
    mode.height = 240;
    before = flips;
    VIDEO_UpdateScreen(NULL);
    assert(flips == before);
    mode.height = 200;
    VIDEO_Resize(640, 480);
    VIDEO_ChangeDepth(16);
    assert(mode.width == 320 && mode.height == 200 && mode.color_mode == 0);
    assert(flushes == flips && audio_pumps > flips && logs >= 3);
    check_guards();
    VIDEO_Shutdown();
    VIDEO_Shutdown();
    assert(surface_count == 0 && palette_count == 0 && VIDEO_GetPalette() == NULL);
    {
        uint8_t source[200][324], target[200][336];
        memset(source, 0x66, sizeof(source));
        memset(target, 0xa5, sizeof(target));
        PAL_PocketCopyRect(&target[0][0], 336, &source[0][0], 324, -2, -2, 4, 4);
        for (y = 0; y < 200; ++y)
            for (x = 0; x < 336; ++x)
                assert(target[y][x] == ((y < 2 && x < 2) ? 0x66 : 0xa5));
        PAL_PocketCopyRect(&target[0][0], 336, &source[0][0], 324,
                          319, 199, INT_MAX, INT_MAX);
        assert(target[199][319] == 0x66 && target[199][320] == 0xa5);
        PAL_PocketCopyRect(&target[0][0], 336, &source[0][0], 324,
                          INT_MAX, INT_MAX, INT_MAX, INT_MAX);
        assert(target[199][320] == 0xa5);
    }
    puts("video backend: palette, mode rejection, pitch, pages, partial frames, transitions, shake, cleanup PASS");
    return 0;
}
