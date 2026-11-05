// Semantic action state with edge detection.
#ifndef MC_PLATFORM_INPUT_H
#define MC_PLATFORM_INPUT_H

#include "actions.h"
#include "platform.h"

typedef struct {
    uint64_t held;      // action bits down this frame
    uint64_t pressed;   // actions that went down this frame
    uint64_t repeat;    // pressed, plus menu auto-repeat

    float move_x, move_y; // -1..1, camera-relative movement
    float look_x, look_y; // -1..1 right stick look

    unsigned int mouse_buttons; // 1 left, 2 right, 4 middle
    unsigned int mouse_pressed;
    int wheel;
    int mouse_moved;
    int mouse_x, mouse_y;
    float mouse_dx, mouse_dy;
} Input;

// Bindings: the four movement directions, then one binding per action.
enum { BIND_MOVE_FORWARD = 0, BIND_MOVE_BACK, BIND_MOVE_LEFT, BIND_MOVE_RIGHT, BIND_ACTIONS };
#define BIND_ACTION(act) (BIND_ACTIONS + (act))
#define BIND_COUNT (BIND_ACTIONS + ACT_COUNT)
#define BIND_NONE (-100)

void input_init(void);
void input_shutdown(void);
void input_poll(Input *in);
void input_ui(Input *in, float dt);

// Binding values: key codes >= 0 (USB HID usage ids, see KEY_* below); mouse buttons
// -1 left, -2 right, -3 middle; -4 wheel up, -5 wheel down; BIND_NONE = unbound.
void input_set_bindings(const int *values, int count);   // BIND_COUNT values, in order
void input_set_binding(int bind, int value);
void input_default_bindings(void);
int input_binding(int action);
const char *input_key_name(int value);       // "E", "LEFT", "MOUSE1"
void input_inject_mouse(float dx, float dy, int wheel, unsigned int buttons, unsigned int pressed, int x, int y);
void input_inject_actions(uint64_t held, uint64_t pressed, int frames);
void input_test_push_key(int scancode, int down);
void input_test_push_motion(float dx, float dy);
void input_test_push_resize(int w, int h);
void input_test_push_focus(int focused);
void input_test_push_button(int button, int down);
void input_test_push_wheel(float y);
void input_get_last(Input *out);
// The key or mouse button pressed since the last call (binding value), or -100. Rebinding.
int input_take_last_press(void);

// ---------------------------------------------------------------- raw keyboard (tools)
// Key codes are USB HID usage ids (SDL scancodes use the same numbers).
enum {
    KEY_A = 4, KEY_Z = 29, KEY_1 = 30, KEY_0 = 39, KEY_RETURN = 40, KEY_ESCAPE = 41,
    KEY_BACKSPACE = 42, KEY_TAB = 43, KEY_SPACE = 44, KEY_MINUS = 45, KEY_EQUALS = 46,
    KEY_F1 = 58, KEY_F5 = 62, KEY_HOME = 74, KEY_PAGEUP = 75, KEY_DELETE = 76, KEY_END = 77,
    KEY_PAGEDOWN = 78, KEY_RIGHT = 79, KEY_LEFT = 80, KEY_DOWN = 81, KEY_UP = 82,
    KEY_KP_1 = 89, KEY_KP_3 = 91, KEY_KP_5 = 93, KEY_KP_7 = 95, KEY_KP_ENTER = 88,
    KEY_LCTRL = 224, KEY_LSHIFT = 225, KEY_LALT = 226, KEY_LGUI = 227,
};
#define KEY_LETTER(c) (KEY_A + ((c) - 'A'))
enum { MOD_CTRL = 1, MOD_SHIFT = 2, MOD_ALT = 4, MOD_GUI = 8 };

int input_key_held(int key);
int input_key_pressed(int key);   // went down during the last poll
int input_key_repeat(int key);    // went down or auto-repeated during the last poll
int input_mods(void);             // MOD_* bits held now
// MOD_CTRL on Windows/Linux, MOD_GUI (Cmd) on macOS: the "shortcut" modifier.
int input_shortcut_mod(void);
// UTF-8 text typed during the last poll (only while text input is on).
void input_text_start(int on);
int input_text_active(void);
const char *input_text_typed(void);
void input_clipboard_set(const char *utf8);
char *input_clipboard_get(void);  // malloc'd, may be NULL
// Mouse in drawable-independent internal pixels while a button is held outside the window too.
void input_set_cursor(int kind);  // 0 arrow, 1 hand, 2 move, 3 text, 4 crosshair

#endif