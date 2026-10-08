/* SPDX-License-Identifier: GPL-3.0-only */
#include "main.h"
#include "of.h"
#include "files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SDL_Window *gpWindow; /* The SDK has no host window; message boxes use its terminal. */
void PAL_PocketAudioPump(void);
void PAL_PocketAudioDrain(void);
void __real_SDL_Delay(Uint32 ms);
void __real_PAL_SaveGame(int slot, WORD times);

BOOL UTIL_GetScreenSize(DWORD *w, DWORD *h)
{
    if (!w || !h) return FALSE;
    *w = 320; *h = 200;
    return TRUE;
}
BOOL UTIL_IsAbsolutePath(const char *path) { return path && path[0] == '/'; }

int UTIL_Platform_Startup(int argc, char *argv[])
{
    const struct of_capabilities *caps = of_get_caps();
    (void)argc; (void)argv;
    if (!caps || caps->magic != OF_CAPS_MAGIC || !of_has_feature(OF_HW_FPU)) {
        fprintf(stderr, "Pocket PAL requires an openfpgaOS rv32imafc runtime with FPU.\n");
        exit(EXIT_FAILURE);
    }
    printf("Pocket PAL: rv32imafc, CPU %u Hz, SDRAM %u bytes\n",
           caps->cpu_freq_hz, caps->sdram_size);
    if (PAL_PocketFilesInit() != 0) {
        SDL_MessageBoxButtonData button = {0, 0, "OK"};
        SDL_MessageBoxData box = {SDL_MESSAGEBOX_ERROR, NULL, "PAL assets unavailable",
            PAL_PocketFileError(), 1, &button, NULL};
        SDL_ShowMessageBox(&box, NULL);
        exit(EXIT_FAILURE);
    }
    of_file_set_idle_hook(PAL_PocketAudioDrain);
    return 0;
}
static void pocket_log(LOGLEVEL level, const char *message, const char *unused)
{
    (void)level; (void)unused;
    if (message) fputs(message, stderr);
}
int UTIL_Platform_Init(int argc, char *argv[])
{
    (void)argc; (void)argv;
    gConfig.fLaunchSetting = FALSE;
    gConfig.fEnableGLSL = FALSE;
    gConfig.fEnableAviPlay = FALSE; /* Candidate is native indexed DOS/RNG. */
    gConfig.eMusicType = MUSIC_OGG;
    gConfig.eCDType = CD_OGG;
    gConfig.iSampleRate = 48000;
    gConfig.iAudioChannels = 2;
    gConfig.iLogLevel = LOGLEVEL_INFO;
    UTIL_LogAddOutputCallback(pocket_log, LOGLEVEL_INFO);
    return 0;
}
void UTIL_Platform_Quit(void)
{
    of_file_set_idle_hook(NULL);
    PAL_PocketFilesShutdown();
}
/* The SDK has cooperative callbacks. Keep OGG production live in every wait. */
void __wrap_SDL_Delay(Uint32 ms)
{
    Uint32 start = SDL_GetTicks();
    do {
        PAL_PocketAudioPump();
        if (ms) __real_SDL_Delay(1);
    } while ((Uint32)(SDL_GetTicks() - start) < ms);
}
/* Upstream save API ignores I/O return values; never silently report failure. */
void __wrap_PAL_SaveGame(int slot, WORD times)
{
    PAL_PocketFileClearError();
    __real_PAL_SaveGame(slot, times);
    if (PAL_PocketFileError()) {
        fprintf(stderr, "SAVE FAILED: %s\n", PAL_PocketFileError());
        char message[256];
        SDL_MessageBoxButtonData button = {0, 0, "OK"};
        snprintf(message, sizeof(message), "%s\n\nProgress was not saved.\nDo not power off yet.",
                 PAL_PocketFileError());
        SDL_MessageBoxData box = {SDL_MESSAGEBOX_ERROR, NULL, "SAVE FAILED",
            message, 1, &button, NULL};
        SDL_ShowMessageBox(&box, NULL);
    }
}

int SDL_ShowMessageBox(const SDL_MessageBoxData *data, int *buttonid)
{
    static const uint32_t terminal_colors[16] = {
        0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
        0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff
    };
    of_video_mode_t previous;
    SDL_Color saved_colors[256];
    int restore = gpScreen && VIDEO_GetPalette();
    if (!data) return -1;
    of_video_get_mode(&previous);
    if (restore) memcpy(saved_colors, VIDEO_GetPalette(), sizeof(saved_colors));
    /* The terminal shares the game CLUT and may change video geometry. */
    of_video_set_display_mode(OF_DISPLAY_TERMINAL);
    of_video_palette_bulk(terminal_colors, 16);
    /* This port disables the launch-settings dialog; all calls are OK-only. */
    printf("\033[0m\033[2J\033[H%s\n\n%s\n\nPress A to continue.\n",
           data->title ? data->title : "PAL", data->message ? data->message : "");
    of_input_poll_p0();
    while (of_btn(OF_BTN_A)) { PAL_PocketAudioPump(); __real_SDL_Delay(1); of_input_poll_p0(); }
    while (!(of_btn(OF_BTN_A))) { PAL_PocketAudioPump(); __real_SDL_Delay(1); of_input_poll_p0(); }
    if (buttonid) *buttonid = data->numbuttons ? data->buttons[0].buttonid : 0;
    if (restore) {
        if (of_video_set_mode(&previous) != 0) return -1;
        VIDEO_SetPalette(saved_colors);
        of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
        VIDEO_RenderCopy();
    }
    return 0;
}
