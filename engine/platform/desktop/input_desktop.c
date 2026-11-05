// Desktop semantic input: keyboard, mouse and gamepad produce actions.
#include "../platform.h"
#include "../input.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PlatMutex *s_input_lock;
static uint64_t s_prev_held;
static float s_hold[4];
static SDL_Gamepad *s_pad;
static SDL_JoystickID s_pad_id;
static Uint64 s_last_ui;
static unsigned int s_mouse_buttons, s_mouse_prev;
static int s_wheel;
static int s_mouse_capture_request;
static int s_last_device;
static unsigned int s_last_mouse_down;   // for capturing a new binding
static int s_last_key_down = -1;
static int s_focused = 1;
static int s_prev_mouse_x, s_prev_mouse_y;
static int s_mouse_moved;
static int s_alt_down, s_alt_used;
static bool s_key_down[512];
static bool s_key_pressed[512], s_key_rep[512];
static char s_text[256];
static int s_text_len, s_text_on;
static SDL_Cursor *s_cursors[5];
static Input s_last_input;
static float s_mouse_event_dx, s_mouse_event_dy;

static const int DIRS[4] = {ACT_NAV_UP, ACT_NAV_DOWN, ACT_NAV_LEFT, ACT_NAV_RIGHT};

static int s_bind[BIND_COUNT];

static void defaults(void)
{
    for (int i = 0; i < BIND_COUNT; i++) s_bind[i] = BIND_NONE;
    s_bind[BIND_MOVE_FORWARD] = SDL_SCANCODE_W;
    s_bind[BIND_MOVE_BACK] = SDL_SCANCODE_S;
    s_bind[BIND_MOVE_LEFT] = SDL_SCANCODE_A;
    s_bind[BIND_MOVE_RIGHT] = SDL_SCANCODE_D;
    s_bind[BIND_ACTION(ACT_CONFIRM)] = SDL_SCANCODE_RETURN;
    s_bind[BIND_ACTION(ACT_BACK)] = SDL_SCANCODE_ESCAPE;
    s_bind[BIND_ACTION(ACT_NAV_UP)] = SDL_SCANCODE_UP;
    s_bind[BIND_ACTION(ACT_NAV_DOWN)] = SDL_SCANCODE_DOWN;
    s_bind[BIND_ACTION(ACT_NAV_LEFT)] = SDL_SCANCODE_LEFT;
    s_bind[BIND_ACTION(ACT_NAV_RIGHT)] = SDL_SCANCODE_RIGHT;
    s_bind[BIND_ACTION(ACT_JUMP)] = SDL_SCANCODE_SPACE;
    s_bind[BIND_ACTION(ACT_SPRINT)] = SDL_SCANCODE_LSHIFT;
    s_bind[BIND_ACTION(ACT_USE)] = SDL_SCANCODE_E;
    s_bind[BIND_ACTION(ACT_ATTACK)] = -1;
    s_bind[BIND_ACTION(ACT_AIM)] = -2;
    s_bind[BIND_ACTION(ACT_MENU)] = SDL_SCANCODE_TAB;
    s_bind[BIND_ACTION(ACT_CAMERA)] = SDL_SCANCODE_C;
    s_bind[BIND_ACTION(ACT_NEXT)] = -5;
    s_bind[BIND_ACTION(ACT_PREV)] = -4;
    s_bind[BIND_ACTION(ACT_ZOOM_IN)] = SDL_SCANCODE_EQUALS;
    s_bind[BIND_ACTION(ACT_ZOOM_OUT)] = SDL_SCANCODE_MINUS;
}

void input_default_bindings(void) { defaults(); }
static int s_inject_actions_frames;
static uint64_t s_inject_held, s_inject_pressed;
static int s_inject_mouse_pending;
static float s_inject_dx, s_inject_dy;
static int s_inject_wheel, s_inject_x = -1, s_inject_y = -1;
static unsigned int s_inject_buttons, s_inject_pressed_buttons;

void plat_mouse_capture(int on)
{
    s_mouse_capture_request = on ? 1 : 0;
    SDL_Window *win = (SDL_Window *)plat_video_window();
    if (!win)
        return;
    if (on) {
        SDL_SetWindowRelativeMouseMode(win, true);
        SDL_HideCursor();
    } else {
        SDL_SetWindowRelativeMouseMode(win, false);
        SDL_ShowCursor();
    }
}
int plat_mouse_captured(void)
{
    SDL_Window *win = (SDL_Window *)plat_video_window();
    if (!win)
        return 1;  // headless: tests treat the virtual mouse as captured
    return SDL_GetWindowRelativeMouseMode(win) ? 1 : 0;
}
int plat_input_last_device(void) { return s_last_device; }
int plat_window_focused(void) { return s_focused; }

void input_set_bindings(const int *values, int count)
{
    for (int i = 0; i < BIND_COUNT && i < count; i++) input_set_binding(i, values[i]);
}

void input_set_binding(int bind, int value)
{
    if (bind < 0 || bind >= BIND_COUNT) return;
    if ((value >= -5 && value < 512) || value == BIND_NONE) s_bind[bind] = value;
}
int plat_key_from_name(const char *name)
{
    if (!strcmp(name, "MOUSE1") || !strcmp(name, "LMB")) return -1;
    if (!strcmp(name, "MOUSE2") || !strcmp(name, "RMB")) return -2;
    if (!strcmp(name, "MOUSE3")) return -3;
    if (!strcmp(name, "WHEEL_UP")) return -4;
    if (!strcmp(name, "WHEEL_DOWN")) return -5;
    SDL_Scancode sc = SDL_GetScancodeFromName(name);
    return sc == SDL_SCANCODE_UNKNOWN ? -1 : (int)sc;
}
int input_binding(int action)
{
    return action >= 0 && action < BIND_COUNT ? s_bind[action] : -100;
}
const char *input_key_name(int value)
{
    static char buf[32];
    if (value == -1) return "MOUSE L";
    if (value == -2) return "MOUSE R";
    if (value == -3) return "MOUSE M";
    if (value == -4) return "WHEEL UP";
    if (value == -5) return "WHEEL DN";
    if (value == -100) return "---";
    const char *n = SDL_GetScancodeName((SDL_Scancode)value);
    if (n && n[0]) return n;
    snprintf(buf, sizeof(buf), "KEY%d", value);
    return buf;
}

static int pad_down(SDL_GamepadButton b) { return s_pad && SDL_GetGamepadButton(s_pad, b); }

// A binding is down: a key, or a mouse button (-1 left, -2 right, -3 middle). Wheel
// bindings (-4/-5) are edges only and handled in the event pump.
static int bind_down(const bool *k, int bind)
{
    int v = s_bind[bind];
    if (v >= 0 && v < 512) return k[v];
    if (v == -1) return (s_mouse_buttons & 1) != 0;
    if (v == -2) return (s_mouse_buttons & 2) != 0;
    if (v == -3) return (s_mouse_buttons & 4) != 0;
    return 0;
}

static uint64_t collect_held(const bool *k)
{
    uint64_t h = 0;
    for (int a = 0; a < ACT_COUNT; a++)
        if (bind_down(k, BIND_ACTION(a))) h |= ACT_BIT(a);
    // A left click also confirms in menus (clicking a row).
    if (s_mouse_buttons & 1) h |= ACT_BIT(ACT_CONFIRM);
    if (pad_down(SDL_GAMEPAD_BUTTON_SOUTH)) h |= ACT_BIT(ACT_JUMP) | ACT_BIT(ACT_CONFIRM);
    if (pad_down(SDL_GAMEPAD_BUTTON_EAST)) h |= ACT_BIT(ACT_BACK);
    if (pad_down(SDL_GAMEPAD_BUTTON_WEST)) h |= ACT_BIT(ACT_USE);
    if (pad_down(SDL_GAMEPAD_BUTTON_NORTH)) h |= ACT_BIT(ACT_CAMERA);
    if (pad_down(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) h |= ACT_BIT(ACT_PREV);
    if (pad_down(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) h |= ACT_BIT(ACT_NEXT);
    if (pad_down(SDL_GAMEPAD_BUTTON_LEFT_STICK)) h |= ACT_BIT(ACT_SPRINT);
    if (pad_down(SDL_GAMEPAD_BUTTON_BACK) || pad_down(SDL_GAMEPAD_BUTTON_START)) h |= ACT_BIT(ACT_MENU);
    if (pad_down(SDL_GAMEPAD_BUTTON_DPAD_UP)) h |= ACT_BIT(ACT_NAV_UP);
    if (pad_down(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) h |= ACT_BIT(ACT_NAV_DOWN);
    if (pad_down(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) h |= ACT_BIT(ACT_NAV_LEFT);
    if (pad_down(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) h |= ACT_BIT(ACT_NAV_RIGHT);
    if (s_pad && SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) h |= ACT_BIT(ACT_AIM);
    if (s_pad && SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) h |= ACT_BIT(ACT_ATTACK);
    return h;
}

static void open_pad(SDL_JoystickID id)
{
    if (s_pad) SDL_CloseGamepad(s_pad);
    s_pad = SDL_OpenGamepad(id);
    s_pad_id = id;
}

void input_shutdown(void)
{
    if (s_pad) { SDL_CloseGamepad(s_pad); s_pad = NULL; }
}

void input_init(void)
{
    defaults();
    s_input_lock = plat_mutex_create("input");
    s_prev_held = 0;
    s_last_ui = SDL_GetTicks();
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids) {
        if (n > 0) open_pad(ids[0]);
        SDL_free(ids);
    }
}

static void pump_events(void)
{
    SDL_Event e;
    memset(s_key_pressed, 0, sizeof(s_key_pressed));
    memset(s_key_rep, 0, sizeof(s_key_rep));
    s_text_len = 0;
    s_text[0] = 0;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            plat_request_exit();
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            // Dragged to any size: the aspect lock keeps 480:272 and the present fits by any
            // factor, so nothing is snapped here.
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            s_focused = 0;
            memset(s_key_down, 0, sizeof(s_key_down));
            plat_mouse_capture(0);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            s_focused = 1;
            plat_mouse_capture(s_mouse_capture_request);
            break;
        case SDL_EVENT_KEY_DOWN:
            s_last_device = 0;
            if (e.key.scancode < 512) {
                s_key_down[e.key.scancode] = true;
                s_key_rep[e.key.scancode] = true;
                if (!e.key.repeat) s_key_pressed[e.key.scancode] = true;
            }
            if (!e.key.repeat) s_last_key_down = (int)e.key.scancode;
            if (e.key.key == SDLK_F11) plat_video_toggle_fullscreen();
            if (e.key.scancode == SDL_SCANCODE_LALT) { s_alt_down = 1; s_alt_used = 0; }
            else if (s_alt_down) s_alt_used = 1;
            break;
        case SDL_EVENT_KEY_UP:
            if (e.key.scancode < 512) s_key_down[e.key.scancode] = false;
            if (e.key.scancode == SDL_SCANCODE_LALT) s_alt_down = 0;
            break;
        case SDL_EVENT_TEXT_INPUT: {
            int n = (int)strlen(e.text.text);
            if (s_text_len + n < (int)sizeof(s_text)) {
                memcpy(s_text + s_text_len, e.text.text, (size_t)n + 1);
                s_text_len += n;
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            if (plat_mouse_captured()) {
                s_mouse_event_dx += e.motion.xrel;
                s_mouse_event_dy += e.motion.yrel;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            s_last_device = 0;
            unsigned int bit = 0;
            if (e.button.button == SDL_BUTTON_LEFT) bit = 1;
            else if (e.button.button == SDL_BUTTON_RIGHT) bit = 2;
            else if (e.button.button == SDL_BUTTON_MIDDLE) bit = 4;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                s_mouse_buttons |= bit;
                s_last_mouse_down = bit;
            } else s_mouse_buttons &= ~bit;
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            s_last_device = 0;
            s_wheel += e.wheel.y > 0 ? 1 : (e.wheel.y < 0 ? -1 : 0);
            // Wheel bindings fire as edges: -4 is up, -5 is down.
            for (int a = 0; a < ACT_COUNT; a++) {
                int v = s_bind[BIND_ACTION(a)];
                if ((e.wheel.y > 0 && v == -4) || (e.wheel.y < 0 && v == -5)) s_inject_pressed |= ACT_BIT(a);
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!s_pad) open_pad(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (s_pad && s_pad_id == (SDL_JoystickID)e.gdevice.which) {
                SDL_CloseGamepad(s_pad);
                s_pad = NULL;
            }
            break;
        default: break;
        }
    }
}

void input_poll(Input *in)
{
    pump_events();
    // Retry relative mode if SDL refused the earlier request (unfocused launch).
    if (s_mouse_capture_request && !plat_mouse_captured()) {
        SDL_Window *win = (SDL_Window *)plat_video_window();
        if (win) {
            SDL_SetWindowRelativeMouseMode(win, true);
            SDL_HideCursor();
        }
    }
    const bool *k = s_key_down;
    uint64_t held = collect_held(k);
    if (s_alt_down) held &= ACT_BIT(ACT_CONFIRM);  // Alt+Tab/Alt+key is not a game action
    held |= s_inject_held;
    uint64_t pressed = (held & ~s_prev_held) | s_inject_pressed;
    in->held = held;
    in->pressed = pressed;
    in->repeat = pressed;

    float ax = 0.0f, ay = 0.0f;
    if (s_bind[BIND_MOVE_FORWARD] >= 0 && k[s_bind[BIND_MOVE_FORWARD]]) ay -= 1.0f;
    if (s_bind[BIND_MOVE_BACK] >= 0 && k[s_bind[BIND_MOVE_BACK]]) ay += 1.0f;
    if (s_bind[BIND_MOVE_LEFT] >= 0 && k[s_bind[BIND_MOVE_LEFT]]) ax -= 1.0f;
    if (s_bind[BIND_MOVE_RIGHT] >= 0 && k[s_bind[BIND_MOVE_RIGHT]]) ax += 1.0f;
    if (s_pad && SDL_GamepadConnected(s_pad)) {
        float lx = SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
        float ly = SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
        if (lx > 0.18f || lx < -0.18f) ax = lx;
        if (ly > 0.18f || ly < -0.18f) ay = ly;
        in->look_x = SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
        in->look_y = SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
        if (in->look_x > -0.15f && in->look_x < 0.15f) in->look_x = 0.0f;
        if (in->look_y > -0.15f && in->look_y < 0.15f) in->look_y = 0.0f;
        if (held || in->look_x || in->look_y) s_last_device = 1;
    } else {
        in->look_x = in->look_y = 0.0f;
    }
    int pad_moving = s_pad && SDL_GamepadConnected(s_pad) &&
                     (SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_LEFTX) > 4000 ||
                      SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_LEFTX) < -4000 ||
                      SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_LEFTY) > 4000 ||
                      SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_LEFTY) < -4000);
    if (!pad_moving) { ax *= 0.6f; ay *= 0.6f; }  // keys give a walk; ACT_SPRINT is the game's to apply
    if (ax > 1.0f) ax = 1.0f;
    if (ax < -1.0f) ax = -1.0f;
    if (ay > 1.0f) ay = 1.0f;
    if (ay < -1.0f) ay = -1.0f;
    in->move_x = ax;
    in->move_y = ay;

    uint64_t now = SDL_GetTicks();
    float dt = (float)(now - s_last_ui) / 1000.0f;
    if (dt < 0.0f || dt > 0.2f) dt = 1.0f / 60.0f;
    s_last_ui = now;
    for (int i = 0; i < 4; i++) {
        if (!(held & ACT_BIT(DIRS[i]))) { s_hold[i] = 0.0f; continue; }
        float before = s_hold[i];
        s_hold[i] += dt;
        if (before >= 0.40f && (int)((s_hold[i] - 0.40f) / 0.11f) != (int)((before - 0.40f) / 0.11f))
            in->repeat |= ACT_BIT(DIRS[i]);
        else if (before < 0.40f && s_hold[i] >= 0.40f)
            in->repeat |= ACT_BIT(DIRS[i]);
    }

    in->mouse_dx = plat_mouse_captured() ? s_mouse_event_dx : 0.0f;
    in->mouse_dy = plat_mouse_captured() ? s_mouse_event_dy : 0.0f;
    s_mouse_event_dx = s_mouse_event_dy = 0.0f;
    if (in->mouse_dx || in->mouse_dy) s_mouse_moved = 1;

    float axp = 0.0f, ayp = 0.0f;
    SDL_GetMouseState(&axp, &ayp);
    unsigned int mbits = s_mouse_buttons;
    in->mouse_buttons = mbits;
    in->mouse_pressed = mbits & ~s_mouse_prev;
    plat_map_mouse((int)axp, (int)ayp, &in->mouse_x, &in->mouse_y);
    if (!plat_mouse_captured()) {
        int adx = in->mouse_x - s_prev_mouse_x, ady = in->mouse_y - s_prev_mouse_y;
        if (adx || ady) s_mouse_moved = 1;
        in->mouse_dx += (float)adx;
        in->mouse_dy += (float)ady;
    }
    s_prev_mouse_x = in->mouse_x;
    s_prev_mouse_y = in->mouse_y;
    in->wheel = s_wheel;
    s_wheel = 0;

    if (s_inject_mouse_pending) {
        in->mouse_dx += s_inject_dx;
        in->mouse_dy += s_inject_dy;
        in->wheel += s_inject_wheel;
        in->mouse_buttons |= s_inject_buttons;
        in->mouse_pressed |= s_inject_pressed_buttons;
        if (s_inject_x >= 0) { in->mouse_x = s_inject_x; s_mouse_moved = 1; }
        if (s_inject_y >= 0) { in->mouse_y = s_inject_y; s_mouse_moved = 1; }
        s_inject_mouse_pending = 0;
    }
    in->mouse_moved = s_mouse_moved;
    s_mouse_moved = 0;
    s_mouse_prev = mbits;
    s_prev_held = held;
    s_inject_pressed = 0;
    s_last_input = *in;
    if (s_inject_actions_frames > 0) {
        in->held |= s_inject_held;
        in->pressed |= s_inject_pressed;
        s_inject_actions_frames--;
        if (s_inject_actions_frames == 0) s_inject_held = s_inject_pressed = 0;
    }
    (void)s_input_lock;
}

void input_test_push_key(int scancode, int down)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = (SDL_Scancode)scancode;
    e.key.key = SDL_GetKeyFromScancode((SDL_Scancode)scancode, SDL_KMOD_NONE, false);
    SDL_PushEvent(&e);
}
void input_test_push_motion(float dx, float dy)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.xrel = dx;
    e.motion.yrel = dy;
    SDL_PushEvent(&e);
}

void input_test_push_button(int button, int down)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = (Uint8)button;
    SDL_PushEvent(&e);
}
void input_test_push_wheel(float y)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.y = y;
    SDL_PushEvent(&e);
}

void input_test_push_resize(int w, int h)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_WINDOW_RESIZED;
    e.window.data1 = w;
    e.window.data2 = h;
    SDL_PushEvent(&e);
}
void input_test_push_focus(int focused)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = focused ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    SDL_PushEvent(&e);
}

void input_inject_actions(uint64_t held, uint64_t pressed, int frames)
{
    s_inject_held = held;
    s_inject_pressed = pressed;
    s_inject_actions_frames = frames > 0 ? frames : 1;
}

void input_get_last(Input *out) { if (out) *out = s_last_input; }

int input_take_last_press(void)
{
    int v = -100;
    if (s_last_key_down >= 0) v = s_last_key_down;
    else if (s_last_mouse_down == 1) v = -1;
    else if (s_last_mouse_down == 2) v = -2;
    else if (s_last_mouse_down == 4) v = -3;
    s_last_key_down = -1;
    s_last_mouse_down = 0;
    return v;
}

void input_inject_mouse(float dx, float dy, int wheel, unsigned int buttons, unsigned int pressed, int x, int y)
{
    s_inject_dx = dx; s_inject_dy = dy; s_inject_wheel = wheel;
    s_inject_buttons = buttons; s_inject_pressed_buttons = pressed;
    s_inject_x = x; s_inject_y = y;
    s_inject_mouse_pending = 1;
}

void input_ui(Input *in, float dt)
{
    static uint64_t prev;
    static float timer;
    uint64_t dir = 0;
    if (in->move_y < -0.55f) dir = ACT_BIT(ACT_NAV_UP);
    else if (in->move_y > 0.55f) dir = ACT_BIT(ACT_NAV_DOWN);
    else if (in->move_x < -0.55f) dir = ACT_BIT(ACT_NAV_LEFT);
    else if (in->move_x > 0.55f) dir = ACT_BIT(ACT_NAV_RIGHT);
    if (dir != prev) {
        in->pressed |= dir;
        in->repeat |= dir;
        timer = 0.4f;
    } else if (dir) {
        timer -= dt;
        if (timer <= 0) { in->repeat |= dir; timer = 0.12f; }
    }
    prev = dir;
}
// ---------------------------------------------------------------- raw keyboard (tools)
int input_key_held(int key) { return key >= 0 && key < 512 && s_key_down[key]; }
int input_key_pressed(int key) { return key >= 0 && key < 512 && s_key_pressed[key]; }
int input_key_repeat(int key) { return key >= 0 && key < 512 && s_key_rep[key]; }

int input_mods(void)
{
    int m = 0;
    if (s_key_down[SDL_SCANCODE_LCTRL] || s_key_down[SDL_SCANCODE_RCTRL]) m |= MOD_CTRL;
    if (s_key_down[SDL_SCANCODE_LSHIFT] || s_key_down[SDL_SCANCODE_RSHIFT]) m |= MOD_SHIFT;
    if (s_key_down[SDL_SCANCODE_LALT] || s_key_down[SDL_SCANCODE_RALT]) m |= MOD_ALT;
    if (s_key_down[SDL_SCANCODE_LGUI] || s_key_down[SDL_SCANCODE_RGUI]) m |= MOD_GUI;
    return m;
}

int input_shortcut_mod(void)
{
#ifdef __APPLE__
    return (input_mods() & (MOD_GUI | MOD_CTRL)) != 0;
#else
    return (input_mods() & MOD_CTRL) != 0;
#endif
}

void input_text_start(int on)
{
    SDL_Window *win = (SDL_Window *)plat_video_window();
    s_text_on = on ? 1 : 0;
    if (!win) return;
    if (on) SDL_StartTextInput(win);
    else SDL_StopTextInput(win);
}
int input_text_active(void) { return s_text_on; }
const char *input_text_typed(void) { return s_text; }

void input_clipboard_set(const char *utf8) { SDL_SetClipboardText(utf8 ? utf8 : ""); }
char *input_clipboard_get(void)
{
    char *t = SDL_GetClipboardText();
    if (!t) return NULL;
    size_t n = strlen(t);
    char *out = (char *)malloc(n + 1);
    if (out) memcpy(out, t, n + 1);
    SDL_free(t);
    return out;
}

void input_set_cursor(int kind)
{
    static const SDL_SystemCursor IDS[5] = {SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_POINTER,
                                            SDL_SYSTEM_CURSOR_MOVE, SDL_SYSTEM_CURSOR_TEXT,
                                            SDL_SYSTEM_CURSOR_CROSSHAIR};
    static int current = -1;
    if (kind < 0 || kind > 4 || kind == current) return;
    if (!s_cursors[kind]) s_cursors[kind] = SDL_CreateSystemCursor(IDS[kind]);
    if (s_cursors[kind]) SDL_SetCursor(s_cursors[kind]);
    current = kind;
}
