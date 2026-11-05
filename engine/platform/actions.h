// Semantic actions. Game code checks these, never raw keys or gamepad buttons.
//
// The first block is the common vocabulary of games and menus with sensible default
// keys and gamepad buttons. ACT_USER_0..15 are free for each game: give them keys with
// input_set_binding(BIND_ACTION(ACT_USER_n), key) and name them in your own code.
#ifndef MC_PLATFORM_ACTIONS_H
#define MC_PLATFORM_ACTIONS_H

#include <stdint.h>

enum {
    ACT_CONFIRM = 0,  // Return, left click, pad A
    ACT_BACK,         // Escape, pad B
    ACT_NAV_UP,       // arrow keys, D-pad
    ACT_NAV_DOWN,
    ACT_NAV_LEFT,
    ACT_NAV_RIGHT,
    ACT_JUMP,         // Space, pad A
    ACT_SPRINT,       // Left Shift, left stick press
    ACT_USE,          // E, pad X
    ACT_ATTACK,       // left mouse, right trigger
    ACT_AIM,          // right mouse, left trigger
    ACT_MENU,         // Tab, pad Start/Back
    ACT_CAMERA,       // C, pad Y
    ACT_NEXT,         // wheel down, right shoulder
    ACT_PREV,         // wheel up, left shoulder
    ACT_ZOOM_IN,      // =
    ACT_ZOOM_OUT,     // -
    ACT_USER_0,       // game-defined actions (no default key)
    ACT_USER_15 = ACT_USER_0 + 15,
    ACT_COUNT
};

#define ACT_BIT(a) (1ull << (a))

#endif
