/* SPDX-License-Identifier: GPL-3.0-only
 * Native openfpgaOS input. Timing/direction semantics follow sdlpal/input.c,
 * Copyright (c) 2009-2011 Wei Mingzhi and 2011-2026 SDLPAL contributors.
 */
#include "main.h"
#include "of_input.h"
#include "input_logic.h"

extern void PAL_PocketAudioPump(void);

volatile PALINPUTSTATE g_InputState;
BOOL g_fUseJoystick = TRUE;
static PAL_PocketInputTracker g_tracker;
static uint32_t g_physical_keys, g_filtered_keys;
static void (*g_init_filter)(void);
static int (*g_event_filter)(const SDL_Event *, volatile PALINPUTSTATE *);
static void (*g_shutdown_filter)(void);

static SDL_Keycode action_key(unsigned bit)
{
    static const SDL_Keycode keys[PAL_POCKET_KEY_COUNT] = {
        SDLK_ESCAPE, SDLK_RETURN, SDLK_DOWN, SDLK_LEFT, SDLK_UP, SDLK_RIGHT,
        SDLK_PAGEUP, SDLK_PAGEDOWN, SDLK_r, SDLK_a, SDLK_d, SDLK_e, SDLK_w,
        SDLK_q, SDLK_s, SDLK_f, SDLK_HOME, SDLK_END
    };
    return keys[bit];
}

/* Preserve PAL_RegisterInputFilter for game-local filters by presenting
 * canonical keyboard edges. A consumed press stays blocked until release. */
static uint32_t filter_keys(uint32_t held)
{
    unsigned bit;
    uint32_t changed = held ^ g_physical_keys;
    if (g_event_filter) {
        for (bit = 0; bit < PAL_POCKET_KEY_COUNT; ++bit) {
            uint32_t key = 1u << bit;
            SDL_Event event;
            if (!(changed & key)) continue;
            memset(&event, 0, sizeof(event));
            event.type = (held & key) ? SDL_KEYDOWN : SDL_KEYUP;
            event.key.state = (held & key) ? SDL_PRESSED : SDL_RELEASED;
            event.key.keysym.sym = action_key(bit);
            event.key.keysym.scancode = SDL_GetScancodeFromKey(event.key.keysym.sym);
            if (g_event_filter(&event, &g_InputState) && (held & key))
                g_filtered_keys |= key;
        }
    }
    g_filtered_keys &= held;
    g_physical_keys = held;
    return held & ~g_filtered_keys;
}

VOID PAL_InitInput(VOID)
{
    memset((void *)&g_InputState, 0, sizeof(g_InputState));
    memset(&g_tracker, 0, sizeof(g_tracker));
    g_InputState.dir = g_InputState.prevdir = kDirUnknown;
    g_physical_keys = g_filtered_keys = 0;
    if (g_init_filter) g_init_filter();
}

VOID PAL_ShutdownInput(VOID)
{
    if (g_shutdown_filter) g_shutdown_filter();
    memset(&g_tracker, 0, sizeof(g_tracker));
    g_physical_keys = g_filtered_keys = 0;
    g_InputState.dir = g_InputState.prevdir = kDirUnknown;
    PAL_ClearKeyState();
}

VOID PAL_ClearKeyState(VOID)
{
    /* Don't discard held state: holding Confirm must not become a fresh
     * press just because the next menu clears the pending action flags. */
    g_InputState.dwKeyPress = 0;
}

VOID PAL_ProcessEvent(VOID)
{
    of_keyboard_state_t keyboard;
    uint32_t keys = 0, pressed = 0, now;
    unsigned player;
    PAL_PocketAudioPump();
    of_input_poll();
    if (g_fUseJoystick) {
        /* Single-player game: either connected pad may operate the UI. */
        for (player = 0; player < OF_MAX_PLAYERS; ++player) {
            of_input_state_t pad;
            memset(&pad, 0, sizeof(pad));
            of_input_state((int)player, &pad);
            keys |= PAL_PocketPadKeys(pad.buttons, pad.joy_lx, pad.joy_ly);
            pressed |= PAL_PocketPadKeys(pad.buttons_pressed, 0, 0);
        }
    }
    memset(&keyboard, 0, sizeof(keyboard));
    of_input_keyboard_state(&keyboard);
    keys |= PAL_PocketKeyboardKeys(&keyboard);
    pressed |= PAL_PocketKeyboardPressed(&keyboard);
    now = SDL_GetTicks();
    /* A press and release between two polls still produces one action, but
     * cannot leave the action held or create later repeat events. */
    PAL_PocketInputUpdate(&g_tracker, &g_InputState, filter_keys(keys | pressed),
                         now, gConfig.fEnableKeyRepeat);
    if (pressed & ~keys)
        PAL_PocketInputUpdate(&g_tracker, &g_InputState, filter_keys(keys),
                             now, gConfig.fEnableKeyRepeat);
    PAL_PocketAudioPump();
}

VOID PAL_RegisterInputFilter(void (*init_filter)(void),
    int (*event_filter)(const SDL_Event *, volatile PALINPUTSTATE *),
    void (*shutdown_filter)(void))
{
    g_init_filter = init_filter;
    g_event_filter = event_filter;
    g_shutdown_filter = shutdown_filter;
}

VOID PAL_SetTouchBounds(DWORD width, DWORD height, SDL_Rect rect)
{
    (void)width; (void)height; (void)rect;
}
