/* SPDX-License-Identifier: GPL-3.0-only */
#include "main.h"
#include "input_logic.h"

CONFIGURATION gConfig;
static of_input_state_t pads[OF_MAX_PLAYERS];
static of_keyboard_state_t keyboard;
static uint32_t ticks;
static int polls, pumps, filter_down, filter_up, initialized, stopped;

void of_input_poll(void) { ++polls; }
uint32_t of_input_state(int player, of_input_state_t *out)
{
    assert(player >= 0 && player < OF_MAX_PLAYERS); *out = pads[player]; return out->buttons;
}
void of_input_keyboard_state(of_keyboard_state_t *out) { *out = keyboard; }
void PAL_PocketAudioPump(void) { ++pumps; }
Uint32 SDL_GetTicks(void) { return ticks; }
SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key)
{
    if (key >= 'a' && key <= 'z') return (SDL_Scancode)(SDL_SCANCODE_A + key - 'a');
    return (SDL_Scancode)(key & ~SDLK_SCANCODE_MASK);
}
static void start_filter(void) { ++initialized; }
static void stop_filter(void) { ++stopped; }
static int filter(const SDL_Event *event, volatile PALINPUTSTATE *state)
{
    (void)state;
    if (event->type == SDL_KEYDOWN) ++filter_down;
    if (event->type == SDL_KEYUP) ++filter_up;
    return event->key.keysym.sym == SDLK_RETURN;
}
static void poll(uint32_t time)
{
    int before = polls;
    ticks = time;
    PAL_ProcessEvent();
    assert(polls == before + 1);
    assert(pumps == polls * 2);
}
static void release_all(void)
{
    memset(pads, 0, sizeof(pads));
    memset(&keyboard, 0, sizeof(keyboard));
    poll(ticks + 1);
    PAL_ClearKeyState();
    assert(g_InputState.dir == kDirUnknown);
}

int main(void)
{
    unsigned i;
    static const struct { uint32_t button, action; } maps[] = {
        {OF_BTN_UP, kKeyUp}, {OF_BTN_DOWN, kKeyDown},
        {OF_BTN_LEFT, kKeyLeft}, {OF_BTN_RIGHT, kKeyRight},
        {OF_BTN_A, kKeySearch}, {OF_BTN_B, kKeyMenu},
        {OF_BTN_X, kKeyUseItem}, {OF_BTN_Y, kKeyStatus},
        {OF_BTN_L1, kKeyPgUp}, {OF_BTN_R1, kKeyPgDn},
        {OF_BTN_L2, kKeyPgUp}, {OF_BTN_R2, kKeyPgDn},
        {OF_BTN_SELECT, kKeyAuto}, {OF_BTN_START, kKeyMenu}
    };
    static const struct { unsigned usage; uint32_t action; } keyboard_maps[] = {
        {SDL_SCANCODE_UP, kKeyUp}, {SDL_SCANCODE_KP_8, kKeyUp},
        {SDL_SCANCODE_DOWN, kKeyDown}, {SDL_SCANCODE_KP_2, kKeyDown},
        {SDL_SCANCODE_LEFT, kKeyLeft}, {SDL_SCANCODE_KP_4, kKeyLeft},
        {SDL_SCANCODE_RIGHT, kKeyRight}, {SDL_SCANCODE_KP_6, kKeyRight},
        {SDL_SCANCODE_ESCAPE, kKeyMenu}, {SDL_SCANCODE_INSERT, kKeyMenu},
        {SDL_SCANCODE_LALT, kKeyMenu}, {SDL_SCANCODE_RALT, kKeyMenu},
        {SDL_SCANCODE_KP_0, kKeyMenu}, {SDL_SCANCODE_RETURN, kKeySearch},
        {SDL_SCANCODE_SPACE, kKeySearch}, {SDL_SCANCODE_KP_ENTER, kKeySearch},
        {SDL_SCANCODE_LCTRL, kKeySearch}, {SDL_SCANCODE_PAGEUP, kKeyPgUp},
        {SDL_SCANCODE_KP_9, kKeyPgUp}, {SDL_SCANCODE_PAGEDOWN, kKeyPgDn},
        {SDL_SCANCODE_KP_3, kKeyPgDn}, {SDL_SCANCODE_HOME, kKeyHome},
        {SDL_SCANCODE_KP_7, kKeyHome}, {SDL_SCANCODE_END, kKeyEnd},
        {SDL_SCANCODE_KP_1, kKeyEnd}, {SDL_SCANCODE_R, kKeyRepeat},
        {SDL_SCANCODE_A, kKeyAuto}, {SDL_SCANCODE_D, kKeyDefend},
        {SDL_SCANCODE_E, kKeyUseItem}, {SDL_SCANCODE_W, kKeyThrowItem},
        {SDL_SCANCODE_Q, kKeyFlee}, {SDL_SCANCODE_F, kKeyForce},
        {SDL_SCANCODE_S, kKeyStatus}
    };
    gConfig.fEnableKeyRepeat = TRUE;
    PAL_InitInput();
    assert(g_InputState.dir == kDirUnknown && g_InputState.dwKeyPress == 0);
    for (i = 0; i < sizeof(maps) / sizeof(maps[0]); ++i) {
        pads[0].buttons = maps[i].button;
        poll(ticks + 1);
        assert(g_InputState.dwKeyPress == maps[i].action);
        release_all();
    }
    pads[0].buttons = OF_BTN_A;
    poll(1000);
    assert(g_InputState.dwKeyPress == kKeySearch);
    PAL_ClearKeyState();
    poll(1199);
    assert(g_InputState.dwKeyPress == 0);
    poll(1200);
    assert(g_InputState.dwKeyPress == kKeySearch);
    PAL_ClearKeyState();
    poll(1274);
    assert(g_InputState.dwKeyPress == 0);
    poll(1275);
    assert(g_InputState.dwKeyPress == kKeySearch);
    release_all();
    gConfig.fEnableKeyRepeat = FALSE;
    pads[0].buttons = OF_BTN_A;
    poll(2000);
    PAL_ClearKeyState();
    poll(10000);
    assert(g_InputState.dwKeyPress == 0);
    release_all();
    gConfig.fEnableKeyRepeat = TRUE;
    pads[0].buttons = OF_BTN_UP;
    poll(11000);
    assert(g_InputState.dir == kDirNorth);
    pads[0].buttons |= OF_BTN_LEFT;
    poll(11001);
    assert(g_InputState.dir == kDirWest && g_InputState.prevdir == kDirNorth);
    pads[0].buttons &= ~OF_BTN_LEFT;
    poll(11002);
    assert(g_InputState.dir == kDirNorth);
    release_all();
    assert(g_InputState.dwKeyMaxCount == 0);
    pads[0].joy_lx = 12000;
    pads[0].joy_ly = -12000;
    poll(12000);
    assert(g_InputState.dwKeyPress == 0);
    pads[0].joy_lx = 12001;
    pads[0].joy_ly = -12001;
    poll(12001);
    assert(g_InputState.dwKeyPress == (kKeyRight | kKeyUp));
    release_all();
    pads[1].buttons = OF_BTN_A;
    poll(13000);
    assert(g_InputState.dwKeyPress == kKeySearch);
    /* Releasing one source doesn't release an action held by another. */
    pads[0].buttons = OF_BTN_A;
    pads[1].buttons = 0;
    PAL_ClearKeyState();
    poll(13001);
    assert(g_InputState.dwKeyPress == 0);
    release_all();
    keyboard.present = 1;
    keyboard.keys[SDL_SCANCODE_R >> 5] = 1u << (SDL_SCANCODE_R & 31);
    keyboard.keys[SDL_SCANCODE_KP_8 >> 5] |= 1u << (SDL_SCANCODE_KP_8 & 31);
    keyboard.modifiers = OF_KEYMOD_LCTRL;
    poll(14000);
    assert(g_InputState.dwKeyPress == (kKeyRepeat | kKeyUp | kKeySearch));
    assert(g_InputState.dir == kDirNorth);
    keyboard.present = 0; /* Unplug with held keys must release everything. */
    PAL_ClearKeyState();
    poll(14001);
    assert(g_InputState.dir == kDirUnknown && g_InputState.dwKeyPress == 0);
    release_all();
    for (i = 0; i < sizeof(keyboard_maps) / sizeof(keyboard_maps[0]); ++i) {
        unsigned usage = keyboard_maps[i].usage;
        keyboard.present = 1;
        keyboard.keys[usage >> 5] = 1u << (usage & 31);
        poll(ticks + 1);
        assert(g_InputState.dwKeyPress == keyboard_maps[i].action);
        release_all();
    }
    for (i = 0; i < OF_KEYBOARD_MAX_USAGE; ++i) {
        unsigned index;
        uint32_t expected = 0;
        for (index = 0; index < sizeof(keyboard_maps) / sizeof(keyboard_maps[0]); ++index)
            if (keyboard_maps[index].usage == i) expected = keyboard_maps[index].action;
        assert(PAL_PocketHIDKey(i) == expected);
    }
    release_all();
    /* The initial delay and interval remain correct across uint32 rollover. */
    pads[0].buttons = OF_BTN_A;
    poll(UINT32_MAX - 100u);
    PAL_ClearKeyState();
    poll(98);
    assert(g_InputState.dwKeyPress == 0);
    poll(99);
    assert(g_InputState.dwKeyPress == kKeySearch);
    release_all();
    pads[0].buttons_pressed = OF_BTN_A;
    pads[0].buttons_released = OF_BTN_A;
    poll(150);
    assert(g_InputState.dwKeyPress == kKeySearch);
    PAL_ClearKeyState();
    pads[0].buttons_pressed = pads[0].buttons_released = 0;
    poll(151);
    assert(g_InputState.dwKeyPress == 0);
    keyboard.present = 1;
    keyboard.keys_pressed[SDL_SCANCODE_E >> 5] = 1u << (SDL_SCANCODE_E & 31);
    poll(152);
    assert(g_InputState.dwKeyPress == kKeyUseItem);
    release_all();
    g_fUseJoystick = FALSE;
    pads[0].buttons = OF_BTN_A;
    poll(200);
    assert(g_InputState.dwKeyPress == 0);
    g_fUseJoystick = TRUE;
    release_all();
    PAL_RegisterInputFilter(start_filter, filter, stop_filter);
    PAL_InitInput();
    assert(initialized == 1);
    pads[0].buttons = OF_BTN_A | OF_BTN_B;
    poll(300);
    assert(g_InputState.dwKeyPress == kKeyMenu && filter_down == 2);
    PAL_ClearKeyState();
    poll(301);
    assert(g_InputState.dwKeyPress == 0 && filter_down == 2);
    release_all();
    assert(filter_up == 2);
    PAL_ShutdownInput();
    assert(stopped == 1 && g_InputState.dwKeyPress == 0 && g_InputState.dir == kDirUnknown);
    puts("input backend: pad, keyboard, analog, second pad, releases, repeats, rollover, filters PASS");
    return 0;
}
