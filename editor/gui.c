#include "gui.h"
#include "engine/render/ui.h"
#include "engine/render/font.h"
#include "engine/core/i18n.h"
#include "engine/render/render_api.h"
#include "engine/render/render_config.h"

static int ED_SCREEN_W_(void) { int w, h; r_ui_size(&w, &h); return w; }
static int ED_SCREEN_H_(void) { int w, h; r_ui_size(&w, &h); return h; }
#define ED_SCREEN_W ED_SCREEN_W_()
#define ED_SCREEN_H ED_SCREEN_H_()

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Gui gui;

GuiId gui_id(const char *s, int n)
{
    unsigned int h = 2166136261u;
    for (; s && *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    h = (h ^ (unsigned int)n) * 16777619u;
    return h ? h : 1;
}

void gui_begin(const Input *in, double time)
{
    gui.in = *in;
    gui.time = time;
    gui.mx = in->mouse_x;
    gui.my = in->mouse_y;
    gui.hot = gui.hot_next;
    gui.hot_next = 0;
    gui.clicked = (in->mouse_pressed & 1) != 0;
    gui.rclicked = (in->mouse_pressed & 2) != 0;
    gui.released = !(in->mouse_buttons & 1);
    gui.dclicked = 0;
    if (gui.clicked) {
        if (time - gui.last_click_t < 0.35 && abs(gui.mx - gui.last_click_x) < 4 && abs(gui.my - gui.last_click_y) < 4)
            gui.dclicked = 1;
        gui.last_click_t = gui.dclicked ? 0 : time;
        gui.last_click_x = gui.mx;
        gui.last_click_y = gui.my;
    }
    gui.mouse_used = 0;
    gui.wheel_used = 0;
    gui.edit_commit = 0;
    gui.tooltip[0] = 0;
    if (!(in->mouse_buttons & 7)) gui.active = 0;
}

static int menu_blocks(void)
{
    return gui.menu_open;
}

int gui_hit(int x, int y, int w, int h)
{
    if (menu_blocks()) return 0;
    return gui.mx >= x && gui.mx < x + w && gui.my >= y && gui.my < y + h;
}

int gui_hover(GuiId id, int x, int y, int w, int h)
{
    if (!gui_hit(x, y, w, h)) return 0;
    if (gui.active && gui.active != id) return 0;
    gui.hot_next = id;
    return 1;
}

void gui_take_mouse(void) { gui.mouse_used = 1; }
int gui_dbl(void) { return gui.dclicked; }

// ---------------------------------------------------------------- drawing
void gui_rect(int x, int y, int w, int h, unsigned int rgb) { if (w > 0 && h > 0) ui_rect(x, y, w, h, rgb, 255); }
void gui_rect_a(int x, int y, int w, int h, unsigned int rgb, int a) { if (w > 0 && h > 0) ui_rect(x, y, w, h, rgb, a); }
void gui_frame(int x, int y, int w, int h, unsigned int rgb) { if (w > 1 && h > 1) ui_frame(x, y, w, h, rgb); }

static unsigned int abgr(unsigned int rgb)
{
    return 0xFF000000u | (rgb & 0xFF) << 16 | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

void gui_line(float x0, float y0, float x1, float y1, unsigned int rgb)
{
    RVertex v[2];
    unsigned int c = abgr(rgb);
    v[0] = (RVertex){x0 + 0.5f, y0 + 0.5f, 0, 0, 0, c};
    v[1] = (RVertex){x1 + 0.5f, y1 + 0.5f, 0, 0, 0, c};
    r_bind_texture(NULL);
    r_draw(R_PRIM_LINES, v, 2);
}

void gui_circle(float cx, float cy, float r, unsigned int rgb)
{
    int seg = r < 8 ? 12 : (r < 40 ? 24 : 40);
    RVertex v[80];
    unsigned int c = abgr(rgb);
    for (int i = 0; i < seg; i++) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        v[i * 2] = (RVertex){cx + cosf(a0) * r, cy + sinf(a0) * r, 0, 0, 0, c};
        v[i * 2 + 1] = (RVertex){cx + cosf(a1) * r, cy + sinf(a1) * r, 0, 0, 0, c};
    }
    r_bind_texture(NULL);
    r_draw(R_PRIM_LINES, v, seg * 2);
}

void gui_disc(float cx, float cy, float r, unsigned int rgb)
{
    int seg = 12;
    RVertex v[36];
    unsigned int c = abgr(rgb);
    for (int i = 0; i < seg; i++) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        v[i * 3] = (RVertex){cx, cy, 0, 0, 0, c};
        v[i * 3 + 1] = (RVertex){cx + cosf(a1) * r, cy + sinf(a1) * r, 0, 0, 0, c};
        v[i * 3 + 2] = (RVertex){cx + cosf(a0) * r, cy + sinf(a0) * r, 0, 0, 0, c};
    }
    r_bind_texture(NULL);
    r_set_cull(0);
    r_draw(R_PRIM_TRIANGLES, v, seg * 3);
}

void gui_panel(int x, int y, int w, int h)
{
    gui_rect(x, y, w, h, C_PANEL);
    gui_frame(x, y, w, h, C_EDGE);
}

int gui_header(int x, int y, int w, const char *title)
{
    gui_rect(x, y, w, 18, C_PANEL2);
    gui_rect(x, y + 18, w, 1, C_EDGE_IN);
    font_draw(x + 8, y + 1, C_BRIGHT, title);
    return 20;
}

int gui_text(int x, int y, unsigned int rgb, const char *s) { return font_draw(x, y, rgb, s); }

int gui_text_clip(int x, int y, int w, unsigned int rgb, const char *s)
{
    if (!s) return 0;
    int n = font_fit(s, w);
    if (s[n] && n > 0) {
        n = font_fit(s, w - 8);
        int dw = font_draw_n(x, y, rgb, s, n);
        font_draw(x + dw, y, C_DIM, "…");
        return w;
    }
    return font_draw_n(x, y, rgb, s, n);
}

int gui_textf(int x, int y, unsigned int rgb, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return font_draw(x, y, rgb, buf);
}

int gui_text_wrap(int x, int y, int w, unsigned int rgb, const char *s, int draw)
{
    int lines = 0;
    while (s && *s) {
        int nl = 0;
        while (s[nl] && s[nl] != '\n') nl++;
        int n = font_fit(s, w);
        if (n > nl) n = nl;
        if (n == 0 && nl > 0) n = utf8_next(s, &(unsigned int){0});
        // Prefer breaking at a space for Latin text (CJK breaks anywhere).
        int latin = 1;
        for (int k = 0; k < n; k++) if ((unsigned char)s[k] >= 0x80) latin = 0;
        if (n < nl && latin) {
            int k = n;
            while (k > 0 && s[k] != ' ') k--;
            if (k > n / 2) n = k + 1;
        }
        if (draw) font_draw_n(x, y + lines * font_line_h(), rgb, s, n);
        lines++;
        s += n;
        if (*s == '\n' && n >= nl) s++;
    }
    return lines ? lines : 1;
}

void gui_clip(int x, int y, int w, int h) { ui_clip(x, y, w, h); }
void gui_unclip(void) { ui_clip_reset(); }

void gui_tip(const char *s)
{
    snprintf(gui.tooltip, sizeof(gui.tooltip), "%s", s ? s : "");
    gui.tip_x = gui.mx + 12;
    gui.tip_y = gui.my + 14;
}

// ---------------------------------------------------------------- widgets
int gui_button_id(GuiId id, const char *label, int x, int y, int w, int h, int on)
{
    int over = gui_hover(id, x, y, w, h);
    int down = over && (gui.in.mouse_buttons & 1) && gui.active == id;
    if (over && gui.clicked) { gui.active = id; gui_take_mouse(); }
    gui_rect(x, y, w, h, down ? 0x2A2C30 : (on ? C_SEL : (over ? 0x3A3D43 : 0x33353A)));
    gui_frame(x, y, w, h, on ? C_ACCENT : (over ? 0x50545B : C_EDGE));
    int tw = font_width(label);
    unsigned int col = on ? C_BRIGHT : (over ? C_BRIGHT : C_TEXT);
    font_draw(x + (w - tw) / 2, y + (h - font_cell_h()) / 2 + (down ? 1 : 0), col, label);
    // Fire on release over the button, like a desktop button.
    if (gui.active == id && !(gui.in.mouse_buttons & 1)) return 0;
    if (over && gui.clicked) return 1;
    return 0;
}

int gui_button(const char *key, int x, int y, int w, int h)
{
    return gui_button_id(gui_id(key, x * 7919 + y), tr(key), x, y, w, h, 0);
}

int gui_tab(const char *key, int x, int y, int w, int on)
{
    GuiId id = gui_id(key, 0x7AB);
    const char *label = tr(key);
    int over = gui_hover(id, x, y, w, 18);
    gui_rect(x, y, w, 18, on ? C_PANEL : (over ? 0x2A2C30 : C_BG));
    if (on) gui_rect(x, y + 16, w, 2, C_ACCENT);
    int tw = font_width(label);
    font_draw(x + (w - tw) / 2, y + 1, on ? C_BRIGHT : (over ? C_TEXT : C_DIM), label);
    if (over && gui.clicked) { gui_take_mouse(); return 1; }
    return 0;
}

int gui_check(const char *key, int x, int y, int *v)
{
    GuiId id = gui_id(key, x * 31 + y);
    const char *label = tr(key);
    int w = 14 + font_width(label);
    int over = gui_hover(id, x, y, w, 14);
    gui_rect(x, y + 2, 11, 11, *v ? C_ACCENT : 0x1B1C1F);
    gui_frame(x, y + 2, 11, 11, *v ? C_ACCENT : (over ? 0x50545B : C_EDGE));
    if (*v) { gui_line(x + 2.0f, y + 7.0f, x + 4.0f, y + 10.0f, C_BRIGHT); gui_line(x + 4.0f, y + 10.0f, x + 9.0f, y + 4.0f, C_BRIGHT); }
    font_draw(x + 14, y - 1, over ? C_BRIGHT : C_TEXT, label);
    if (over && gui.clicked) { *v = !*v; gui_take_mouse(); return 1; }
    return 0;
}

int gui_row(GuiId id, int x, int y, int w, int h, int selected)
{
    int over = gui_hover(id, x, y, w, h);
    if (selected) gui_rect(x, y, w, h, C_SEL);
    else if (over) gui_rect(x, y, w, h, 0x303237);
    if (over && gui.clicked) { gui_take_mouse(); return 1; }
    return 0;
}

void gui_focus(GuiId id, const char *initial)
{
    gui.focus = id;
    gui.edit_owner = id;
    snprintf(gui.edit_buf, sizeof(gui.edit_buf), "%s", initial ? initial : "");
    gui.edit_caret = (int)strlen(gui.edit_buf);
}

int gui_focused(GuiId id) { return gui.focus == id; }

static char s_injected[512];
void gui_type_text(const char *t) { snprintf(s_injected, sizeof(s_injected), "%s", t ? t : ""); }

static int edit_keys(void)
{
    // Returns 1 = commit, -1 = cancel, 0 = keep editing.
    const char *t = s_injected[0] ? s_injected : input_text_typed();
    int len = (int)strlen(gui.edit_buf);
    if (t && *t) {
        int n = (int)strlen(t);
        if (len + n < (int)sizeof(gui.edit_buf) - 1) {
            memmove(gui.edit_buf + gui.edit_caret + n, gui.edit_buf + gui.edit_caret, (size_t)(len - gui.edit_caret + 1));
            memcpy(gui.edit_buf + gui.edit_caret, t, (size_t)n);
            gui.edit_caret += n;
            len += n;
        }
        s_injected[0] = 0;
    }
    if (input_shortcut_mod() && input_key_pressed(KEY_LETTER('V'))) {
        char *c = input_clipboard_get();
        if (c) {
            for (char *p = c; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
            int n = (int)strlen(c);
            if (len + n < (int)sizeof(gui.edit_buf) - 1) {
                memmove(gui.edit_buf + gui.edit_caret + n, gui.edit_buf + gui.edit_caret, (size_t)(len - gui.edit_caret + 1));
                memcpy(gui.edit_buf + gui.edit_caret, c, (size_t)n);
                gui.edit_caret += n;
            }
            free(c);
        }
    }
    if (input_shortcut_mod() && input_key_pressed(KEY_LETTER('C'))) input_clipboard_set(gui.edit_buf);
    if (input_key_repeat(KEY_BACKSPACE) && gui.edit_caret > 0) {
        int n = utf8_prev_len(gui.edit_buf, gui.edit_caret);
        memmove(gui.edit_buf + gui.edit_caret - n, gui.edit_buf + gui.edit_caret, strlen(gui.edit_buf + gui.edit_caret) + 1);
        gui.edit_caret -= n;
    }
    if (input_key_repeat(KEY_DELETE) && gui.edit_buf[gui.edit_caret]) {
        unsigned int cp;
        int n = utf8_next(gui.edit_buf + gui.edit_caret, &cp);
        memmove(gui.edit_buf + gui.edit_caret, gui.edit_buf + gui.edit_caret + n, strlen(gui.edit_buf + gui.edit_caret + n) + 1);
    }
    if (input_key_repeat(KEY_LEFT) && gui.edit_caret > 0) gui.edit_caret -= utf8_prev_len(gui.edit_buf, gui.edit_caret);
    if (input_key_repeat(KEY_RIGHT) && gui.edit_buf[gui.edit_caret]) {
        unsigned int cp;
        gui.edit_caret += utf8_next(gui.edit_buf + gui.edit_caret, &cp);
    }
    if (input_key_pressed(KEY_HOME)) gui.edit_caret = 0;
    if (input_key_pressed(KEY_END)) gui.edit_caret = (int)strlen(gui.edit_buf);
    if (input_key_pressed(KEY_RETURN) || input_key_pressed(KEY_KP_ENTER) || input_key_pressed(KEY_TAB)) return 1;
    if (input_key_pressed(KEY_ESCAPE)) return -1;
    return 0;
}

static void draw_field(int x, int y, int w, const char *s, int focused, int over, int caret)
{
    gui_rect(x, y, w, 16, 0x1B1C1F);
    gui_frame(x, y, w, 16, focused ? C_ACCENT : (over ? 0x50545B : C_EDGE));
    gui_clip(x + 1, y + 1, w - 2, 14);
    int scroll = 0;
    if (focused) {
        int cw = font_width_n(s, caret);
        if (cw > w - 8) scroll = cw - (w - 8);
    }
    font_draw(x + 3 - scroll, y, focused ? C_BRIGHT : C_TEXT, s);
    if (focused && fmod(gui.time, 1.0) < 0.6) {
        int cx = x + 3 - scroll + font_width_n(s, caret);
        gui_rect(cx, y + 2, 1, 12, C_ACCENT);
    }
    gui_unclip();
}

int gui_field(GuiId id, int x, int y, int w, char *buf, int size)
{
    int over = gui_hover(id, x, y, w, 16);
    if (gui.focus != id) {
        if (over && gui.clicked) {
            gui_focus(id, buf);
            gui.edit_caret = font_index_at(buf, gui.mx - x - 3);
            gui_take_mouse();
        }
        draw_field(x, y, w, buf, 0, over, 0);
        return 0;
    }
    if (over && gui.clicked) { gui.edit_caret = font_index_at(gui.edit_buf, gui.mx - x - 3); gui_take_mouse(); }
    int r = edit_keys();
    if (!over && gui.clicked && !gui.mouse_used) r = 1;  // clicked elsewhere
    draw_field(x, y, w, gui.edit_buf, 1, over, gui.edit_caret);
    if (r == 0) return 0;
    gui.focus = 0;
    if (r < 0) return 0;
    int changed = strcmp(buf, gui.edit_buf) != 0;
    snprintf(buf, (size_t)size, "%s", gui.edit_buf);
    gui.edit_commit = 1;
    return changed || r == 1;
}

int gui_number(GuiId id, int x, int y, int w, float *v, float step)
{
    char tmp[64];
    if (gui.focus == id) {
        snprintf(tmp, sizeof(tmp), "%g", *v);
        if (gui_field(id, x, y, w, tmp, sizeof(tmp))) {
            char *end;
            float nv = strtof(tmp, &end);
            if (end != tmp && nv != *v) { *v = nv; return 1; }
        }
        return 0;
    }
    int over = gui_hover(id, x, y, w, 16);
    static int moved, last_x;
    if (over && gui.clicked) { gui.active = id; moved = 0; last_x = gui.mx; gui_take_mouse(); }
    int changed = 0;
    if (gui.active == id) {
        if (gui.in.mouse_buttons & 1) {
            int dx = gui.mx - last_x;
            if (dx) {
                float k = (input_mods() & MOD_SHIFT) ? 0.1f : 1.0f;
                *v += dx * step * k;
                *v = roundf(*v / (step * k)) * (step * k);
                moved = 1;
                changed = 1;
                last_x = gui.mx;
            }
            input_set_cursor(2);
        } else {
            gui.active = 0;
            if (!moved) {
                snprintf(tmp, sizeof(tmp), "%g", *v);
                gui_focus(id, tmp);
            }
        }
    }
    snprintf(tmp, sizeof(tmp), "%.3g", *v);
    if (fabsf(*v) >= 1000.0f) snprintf(tmp, sizeof(tmp), "%.0f", *v);
    gui_rect(x, y, w, 16, 0x1B1C1F);
    gui_frame(x, y, w, 16, gui.active == id ? C_ACCENT : (over ? 0x50545B : C_EDGE));
    int tw = font_width(tmp);
    font_draw(x + w - tw - 4, y, over ? C_BRIGHT : C_TEXT, tmp);
    return changed;
}

int gui_drag(GuiId id, int x, int y, int w, int h, int button)
{
    unsigned int bit = button == 1 ? 2u : (button == 2 ? 4u : 1u);
    int over = gui_hover(id, x, y, w, h);
    if (over && (gui.in.mouse_pressed & bit)) {
        gui.active = id;
        gui.pmx = gui.mx;
        gui.pmy = gui.my;
        gui_take_mouse();
    }
    if (gui.active == id) {
        if (gui.in.mouse_buttons & bit) return 1;
        gui.active = 0;
    }
    return 0;
}

// ---------------------------------------------------------------- popup menu
void gui_menu_open(GuiId owner, const char *const *items, int n)
{
    if (n > 24) n = 24;
    gui.menu_open = 1;
    gui.menu_owner = owner;
    gui.menu_n = n;
    gui.menu_pick = -1;
    int w = 80;
    for (int i = 0; i < n; i++) {
        snprintf(gui.menu_items[i], sizeof(gui.menu_items[i]), "%s", items[i]);
        int tw = font_width(items[i]) + 24;
        if (tw > w) w = tw;
    }
    gui.menu_w = w;
    gui.menu_x = gui.mx;
    gui.menu_y = gui.my;
    if (gui.menu_x + w > ED_SCREEN_W - 2) gui.menu_x = ED_SCREEN_W - 2 - w;
    if (gui.menu_y + n * 18 + 4 > ED_SCREEN_H - 2) gui.menu_y = ED_SCREEN_H - 2 - n * 18 - 4;
    gui_take_mouse();
    gui.clicked = 0;
}

void gui_menu_open_keys(GuiId owner, const char *const *keys, int n)
{
    const char *items[24];
    if (n > 24) n = 24;
    for (int i = 0; i < n; i++) items[i] = keys[i][0] == '-' ? "-" : tr(keys[i]);
    gui_menu_open(owner, items, n);
}

int gui_menu(GuiId owner)
{
    if (gui.menu_owner != owner || gui.menu_pick < 0) return -1;
    int p = gui.menu_pick;
    gui.menu_pick = -1;
    gui.menu_owner = 0;
    return p;
}

int gui_menu_active(void) { return gui.menu_open; }

void gui_end(void)
{
    if (gui.menu_open) {
        int x = gui.menu_x, y = gui.menu_y, w = gui.menu_w, h = gui.menu_n * 18 + 4;
        gui_rect_a(x + 3, y + 3, w, h, 0x000000, 140);
        gui_rect(x, y, w, h, C_PANEL);
        gui_frame(x, y, w, h, C_EDGE);
        int inside = gui.mx >= x && gui.mx < x + w && gui.my >= y && gui.my < y + h;
        for (int i = 0; i < gui.menu_n; i++) {
            int ry = y + 2 + i * 18;
            int sep = gui.menu_items[i][0] == '-';
            int over = inside && gui.my >= ry && gui.my < ry + 18 && !sep;
            if (sep) { gui_rect(x + 6, ry + 8, w - 12, 1, C_EDGE_IN); continue; }
            if (over) gui_rect(x + 2, ry, w - 4, 18, C_SEL);
            font_draw(x + 10, ry + 1, over ? C_BRIGHT : C_TEXT, gui.menu_items[i]);
            if (over && (gui.in.mouse_pressed & 1)) { gui.menu_pick = i; gui.menu_open = 0; }
        }
        if (!inside && (gui.in.mouse_pressed & 3)) { gui.menu_open = 0; gui.menu_owner = 0; }
        if (input_key_pressed(KEY_ESCAPE)) { gui.menu_open = 0; gui.menu_owner = 0; }
    }
    if (gui.tooltip[0] && !gui.menu_open) {
        int w = font_width(gui.tooltip) + 10;
        int x = gui.tip_x, y = gui.tip_y;
        if (x + w > ED_SCREEN_W - 2) x = ED_SCREEN_W - 2 - w;
        if (y + 18 > ED_SCREEN_H - 2) y = gui.my - 22;
        gui_rect(x, y, w, 18, 0x17181B);
        gui_frame(x, y, w, 18, C_EDGE);
        font_draw(x + 5, y + 1, C_TEXT, gui.tooltip);
    }
    if (input_text_active() != (gui.focus != 0)) input_text_start(gui.focus != 0);
}
