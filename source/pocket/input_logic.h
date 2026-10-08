/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PAL_POCKET_INPUT_LOGIC_H
#define PAL_POCKET_INPUT_LOGIC_H

#include "input.h"
#include "of_input_types.h"

#define PAL_POCKET_KEY_COUNT 18
#define PAL_POCKET_ANALOG_DEADZONE 12000

typedef struct {
    uint32_t held;
    uint32_t next_repeat[PAL_POCKET_KEY_COUNT];
} PAL_PocketInputTracker;

static inline uint32_t PAL_PocketPadKeys(uint32_t buttons, int16_t x, int16_t y)
{
    uint32_t keys = 0;
    if ((buttons & OF_BTN_UP) || y < -PAL_POCKET_ANALOG_DEADZONE) keys |= kKeyUp;
    if ((buttons & OF_BTN_DOWN) || y > PAL_POCKET_ANALOG_DEADZONE) keys |= kKeyDown;
    if ((buttons & OF_BTN_LEFT) || x < -PAL_POCKET_ANALOG_DEADZONE) keys |= kKeyLeft;
    if ((buttons & OF_BTN_RIGHT) || x > PAL_POCKET_ANALOG_DEADZONE) keys |= kKeyRight;
    if (buttons & OF_BTN_A) keys |= kKeySearch;
    if (buttons & (OF_BTN_B | OF_BTN_START)) keys |= kKeyMenu;
    if (buttons & OF_BTN_X) keys |= kKeyUseItem;
    if (buttons & OF_BTN_Y) keys |= kKeyStatus;
    if (buttons & (OF_BTN_L1 | OF_BTN_L2)) keys |= kKeyPgUp;
    if (buttons & (OF_BTN_R1 | OF_BTN_R2)) keys |= kKeyPgDn;
    if (buttons & OF_BTN_SELECT) keys |= kKeyAuto;
    return keys;
}

/* USB HID usage values equal SDL scancodes. Only a real dock keyboard reaches
 * this mapper; it is independent of the SDK's generic pad-to-keyboard map. */
static inline uint32_t PAL_PocketHIDKey(unsigned usage)
{
    switch (usage) {
    case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: return kKeyUp;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: return kKeyDown;
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: return kKeyLeft;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: return kKeyRight;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_INSERT:
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: case SDL_SCANCODE_KP_0: return kKeyMenu;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE:
    case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_LCTRL: return kKeySearch;
    case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_KP_9: return kKeyPgUp;
    case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_KP_3: return kKeyPgDn;
    case SDL_SCANCODE_HOME: case SDL_SCANCODE_KP_7: return kKeyHome;
    case SDL_SCANCODE_END: case SDL_SCANCODE_KP_1: return kKeyEnd;
    case SDL_SCANCODE_R: return kKeyRepeat;
    case SDL_SCANCODE_A: return kKeyAuto;
    case SDL_SCANCODE_D: return kKeyDefend;
    case SDL_SCANCODE_E: return kKeyUseItem;
    case SDL_SCANCODE_W: return kKeyThrowItem;
    case SDL_SCANCODE_Q: return kKeyFlee;
    case SDL_SCANCODE_F: return kKeyForce;
    case SDL_SCANCODE_S: return kKeyStatus;
    default: return 0;
    }
}

static inline uint32_t PAL_PocketKeyboardKeys(const of_keyboard_state_t *keyboard)
{
    unsigned usage;
    uint32_t keys = 0;
    if (!keyboard || !keyboard->present) return 0;
    for (usage = 0; usage < OF_KEYBOARD_MAX_USAGE; ++usage)
        if (keyboard->keys[usage >> 5] & (1u << (usage & 31)))
            keys |= PAL_PocketHIDKey(usage);
    if (keyboard->modifiers & OF_KEYMOD_LCTRL) keys |= kKeySearch;
    if (keyboard->modifiers & (OF_KEYMOD_LALT | OF_KEYMOD_RALT)) keys |= kKeyMenu;
    return keys;
}

/* Newer runtimes latch complete sub-frame taps in the edge bitmaps. */
static inline uint32_t PAL_PocketKeyboardPressed(const of_keyboard_state_t *keyboard)
{
    of_keyboard_state_t edges;
    if (!keyboard || !keyboard->present) return 0;
    edges = *keyboard;
    memcpy(edges.keys, keyboard->keys_pressed, sizeof(edges.keys));
    edges.modifiers = keyboard->modifiers_pressed;
    return PAL_PocketKeyboardKeys(&edges);
}

static inline PALDIRECTION PAL_PocketKeyDirection(uint32_t key)
{
    switch (key) {
    case kKeyDown: return kDirSouth;
    case kKeyLeft: return kDirWest;
    case kKeyUp: return kDirNorth;
    case kKeyRight: return kDirEast;
    default: return kDirUnknown;
    }
}

static inline void PAL_PocketInputUpdate(PAL_PocketInputTracker *tracker,
    volatile PALINPUTSTATE *state, uint32_t held, uint32_t now, int repeat)
{
    unsigned bit, direction;
    PALDIRECTION previous = state->dir, current = kDirUnknown;
    uint32_t newest = 0;
    for (bit = 0; bit < PAL_POCKET_KEY_COUNT; ++bit) {
        uint32_t key = 1u << bit;
        PALDIRECTION dir = PAL_PocketKeyDirection(key);
        if (!(held & key)) {
            if (dir != kDirUnknown) state->dwKeyOrder[dir] = 0;
            tracker->next_repeat[bit] = 0;
        } else if (!(tracker->held & key)) {
            state->dwKeyPress |= key;
            tracker->next_repeat[bit] = now + 200u;
            if (dir != kDirUnknown) state->dwKeyOrder[dir] = ++state->dwKeyMaxCount;
        } else if (repeat && (int32_t)(now - tracker->next_repeat[bit]) >= 0) {
            state->dwKeyPress |= key;
            tracker->next_repeat[bit] = now + 75u;
        }
    }
    for (direction = 0; direction < 4; ++direction) {
        if (state->dwKeyOrder[direction] > newest) {
            newest = state->dwKeyOrder[direction];
            current = (PALDIRECTION)direction;
        }
    }
    if (current != previous) state->prevdir = previous;
    state->dir = current;
    if (!newest) state->dwKeyMaxCount = 0;
    tracker->held = held;
}
#endif
