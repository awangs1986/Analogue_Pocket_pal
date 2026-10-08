/* SPDX-License-Identifier: GPL-3.0-only */
/* Game-local additions to the partial SDK SDL API; SDK remains pristine. */
#ifndef PAL_POCKET_SDL_MISSING_H
#define PAL_POCKET_SDL_MISSING_H
#include <SDL.h>
typedef struct SDL_MessageBoxButtonData {
    Uint32 flags; int buttonid; const char *text;
} SDL_MessageBoxButtonData;
typedef struct SDL_MessageBoxData {
    Uint32 flags; SDL_Window *window; const char *title; const char *message;
    int numbuttons; const SDL_MessageBoxButtonData *buttons; const void *colorScheme;
} SDL_MessageBoxData;
int SDL_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid);
#endif
