/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PAL_CONFIG_H
#define PAL_CONFIG_H
#define PAL_HAS_OGG 1
#define PAL_HAS_OPUS 0
#define PAL_HAS_MP3 0
#define PAL_HAS_NATIVEMIDI 0
#define PAL_HAS_JOYSTICKS 0
#define PAL_HAS_SDLCD 0
#define PAL_HAS_GLSL 0
#define PAL_HAS_CONFIG_PAGE 0
#define PAL_HAS_PLATFORM_SPECIFIC_UTILS 1
#define PAL_HAS_PLATFORM_STARTUP 1
#define PAL_FILESYSTEM_IGNORE_CASE 1
#define PAL_NO_LAUNCH_UI 1
#define PAL_SCALE_SCREEN TRUE
#define PAL_PREFIX "./"
#define PAL_SAVE_PREFIX "./"
#define PAL_DEFAULT_WINDOW_WIDTH 320
#define PAL_DEFAULT_WINDOW_HEIGHT 200
#define PAL_DEFAULT_TEXTURE_WIDTH 320
#define PAL_DEFAULT_TEXTURE_HEIGHT 200
#define PAL_DEFAULT_FULLSCREEN_HEIGHT 200
#define PAL_VIDEO_INIT_FLAGS 0
#define PAL_SDL_INIT_FLAGS (SDL_INIT_VIDEO | SDL_INIT_TIMER)
#define PAL_PLATFORM "Analogue Pocket / openfpgaOS RISC-V"
#define PAL_CREDIT NULL
#define PAL_PORTYEAR "2026"
#define PLATFORM_DEFAULT_SAMPLERATE 48000
#define SDL_isspace(c) isspace((unsigned char)(c))
#define SDL_mutexP SDL_LockMutex
#define SDL_mutexV SDL_UnlockMutex
#include "sdl_missing.h"
#include <sys/time.h>
#include <ctype.h>
#endif
