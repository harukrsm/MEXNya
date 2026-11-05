// Side panels: tool strip, scene layers, hierarchy, inspector, notes and the console.
#include "ed.h"
#include "gui.h"
#include "engine/platform/platform.h"
#include "engine/platform/file.h"
#include "engine/render/font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LABEL_W 76   // inspector label column, wide enough for every language
static int s_scroll_scenes, s_scroll_objs, s_scroll_notes, s_scroll_log[CH_COUNT];
static char s_rename[128];
static GuiId s_rename_id;
static char s_lua[512];
static char s_reply[256];

static Json *args_scene(void)
{
    Json *a = json_obj();
    json_sets(a, "scene", scene_id(cur_scene()));
    return a;
}

// ---------------------------------------------------------------- tool strip
static const struct { const char *glyph, *tip; int key; } TOOLS[TOOL_COUNT] = {
    {"", "tool.select", 'Q'},
    {"", "tool.move", 'W'},
    {"", "tool.rotate", 'E'},
    {"", "tool.scale", 'R'},
    {"", "tool.box", 'B'},
    {"", "tool.circle", 'C'},
    {"", "tool.arrow", 'A'},
    {"", "tool.pin", 'N'},
    {"", "tool.pen", 'P'},
};

// Tool icons drawn as lines, so they stay sharp at any UI scale.
static void tool_icon(int tool, float cx, float cy, unsigned int c)
{
    switch (tool) {
    case TOOL_SELECT:   // pointer
        gui_line(cx - 4, cy - 6, cx - 4, cy + 5, c);
        gui_line(cx - 4, cy - 6, cx + 5, cy + 3, c);
        gui_line(cx - 4, cy + 5, cx - 1, cy + 2, c);
        gui_line(cx - 1, cy + 2, cx + 5, cy + 3, c);
        gui_line(cx - 1, cy + 2, cx + 2, cy + 7, c);
        break;
    case TOOL_MOVE:     // four arrows
        gui_line(cx - 7, cy, cx + 7, cy, c);
        gui_line(cx, cy - 7, cx, cy + 7, c);
        gui_line(cx - 7, cy, cx - 4, cy - 3, c); gui_line(cx - 7, cy, cx - 4, cy + 3, c);
        gui_line(cx + 7, cy, cx + 4, cy - 3, c); gui_line(cx + 7, cy, cx + 4, cy + 3, c);
        gui_line(cx, cy - 7, cx - 3, cy - 4, c); gui_line(cx, cy - 7, cx + 3, cy - 4, c);
        gui_line(cx, cy + 7, cx - 3, cy + 4, c); gui_line(cx, cy + 7, cx + 3, cy + 4, c);
        break;
    case TOOL_ROTATE:   // open circle with a head
        for (int i = 0; i < 10; i++) {
            float a0 = 0.6f + i * 0.5f, a1 = 0.6f + (i + 1) * 0.5f;
            gui_line(cx + cosf(a0) * 6, cy + sinf(a0) * 6, cx + cosf(a1) * 6, cy + sinf(a1) * 6, c);
        }
        gui_line(cx + 5, cy + 3, cx + 7, cy - 1, c);
        gui_line(cx + 5, cy + 3, cx + 1, cy + 2, c);
        break;
    case TOOL_SCALE:    // small and large square
        gui_frame((int)cx - 7, (int)cy - 1, 7, 7, c);
        gui_frame((int)cx - 7, (int)cy - 7, 14, 14, c);
        break;
    case TOOL_BOX: gui_frame((int)cx - 7, (int)cy - 5, 14, 11, c); break;
    case TOOL_CIRCLE: gui_circle(cx, cy, 6.5f, c); break;
    case TOOL_ARROW:
        gui_line(cx - 6, cy + 6, cx + 6, cy - 6, c);
        gui_line(cx + 6, cy - 6, cx + 1, cy - 6, c);
        gui_line(cx + 6, cy - 6, cx + 6, cy - 1, c);
        break;
    case TOOL_PIN:
        gui_circle(cx, cy - 2, 4, c);
        gui_line(cx, cy + 2, cx, cy + 7, c);
        break;
    default:            // pen stroke
        gui_line(cx - 7, cy + 3, cx - 3, cy - 2, c);
        gui_line(cx - 3, cy - 2, cx + 1, cy + 2, c);
        gui_line(cx + 1, cy + 2, cx + 7, cy - 4, c);
        break;
    }
}

void toolbar(int x, int y, int w, int h)
{
    gui_rect(x, y, w, h, C_PANEL);
    gui_rect(x + w - 1, y, 1, h, C_EDGE);
    int by = y + 4;
    for (int i = 0; i < TOOL_COUNT; i++) {
        if (i == TOOL_BOX) {
            gui_rect(x + 6, by + 3, w - 12, 1, C_EDGE);
            by += 8;
        }
        GuiId id = gui_id("tool", i);
        if (gui_button_id(id, "", x + 3, by, w - 6, 24, ed.tool == i)) ed.tool = i;
        tool_icon(i, x + w / 2.0f, by + 12.0f, ed.tool == i ? C_BRIGHT : (gui.hot == id ? C_BRIGHT : C_TEXT));
        if (gui.hot == id) gui_tip(T(TOOLS[i].tip));
        by += 26;
    }
    // Keyboard shortcuts (not while typing, not while flying with the right button).
    if (!gui.focus && !(gui.in.mouse_buttons & 2) && !input_shortcut_mod()) {
        for (int i = 0; i < TOOL_COUNT; i++)
            if (input_key_pressed(KEY_LETTER(TOOLS[i].key))) ed.tool = i;
    }
}

// ---------------------------------------------------------------- helpers
static void scroll(int *s, int x, int y, int w, int h, int total, int shown)
{
    if (gui_hit(x, y, w, h) && gui.in.wheel && !gui.wheel_used) {
        *s -= gui.in.wheel * 2;
        gui.wheel_used = 1;
    }
    if (*s > total - shown) *s = total - shown;
    if (*s < 0) *s = 0;
    if (total > shown && shown > 0) {
        int bh = h * shown / total;
        if (bh < 8) bh = 8;
        int by = y + (h - bh) * *s / (total - shown);
        gui_rect(x + w - 3, y, 3, h, C_BG);
        gui_rect(x + w - 3, by, 3, bh, C_EDGE);
    }
}

static int rename_field(GuiId id, int x, int y, int w, const char *op, const char *scene, const char *obj_id, const char *key)
{
    if (s_rename_id != id) return 0;
    if (!gui_focused(id)) gui_focus(id, s_rename);
    if (gui_field(id, x, y, w, s_rename, sizeof(s_rename)) || !gui_focused(id)) {
        if (gui.edit_commit) {
            Json *a = json_obj();
            if (scene) json_sets(a, "scene", scene);
            if (obj_id) json_sets(a, "id", obj_id);
            Json *f = json_set(a, "fields", json_obj());
            json_sets(f, key, s_rename);
            ops_do(op, a);
        }
        s_rename_id = 0;
    }
    return 1;
}

static void kind_badge(int x, int y, const char *t, unsigned int c)
{
    int w = font_width(t) + 6;
    gui_rect(x, y + 2, w, 13, 0x1B1C1F);
    gui_frame(x, y + 2, w, 13, c);
    font_draw(x + 3, y, c, t);
}

// ---------------------------------------------------------------- left: layers + hierarchy
static const char *const ADD_3D[] = {"type.box", "type.plane", "type.sphere", "type.cylinder", "type.cone", "type.model", "type.light", "type.marker", "type.camera"};
static const char *const TYPE_3D[] = {"box", "plane", "sphere", "cylinder", "cone", "model", "light", "marker", "camera"};
static const char *const ADD_2D[] = {"type.rect", "type.text", "type.button", "type.image", "type.panel"};
static const char *const TYPE_2D[] = {"rect", "text", "button", "image", "panel"};

static void add_object(int k)
{
    Json *s = cur_scene();
    if (!s) return;
    int is2d = scene_is_2d(s);
    Json *a = args_scene();
    json_sets(a, "type", is2d ? TYPE_2D[k] : TYPE_3D[k]);
    if (!is2d) {
        // New objects land where the camera looks.
        Cam *c = &ed.cams[ed.cur];
        float p[3] = {roundf(c->target[0] * 2) / 2, k == 1 ? 0 : 0.5f, roundf(c->target[2] * 2) / 2};
        if (k == 6) p[1] = 3;
        json_setv(a, "pos", p, 3);
        if (k == 5) json_sets(a, "model", "models/model.mcm");
    } else {
        float r[4] = {20, 20, 80, 24};
        if (k == 1) { r[2] = 120; r[3] = 16; }
        if (k == 4) { r[2] = 160; r[3] = 90; json_sets(a, "color", "#14181B"); }
        json_setv(a, "rect", r, 4);
    }
    ops_do("object.add", a);
}

void panels_left(int x, int y, int w, int h)
{
    gui_rect(x, y, w, h, C_PANEL);
    gui_rect(x + w - 1, y, 1, h, C_EDGE);
    int half = h * 2 / 5;
    // Layers (top of the list = top layer, like a paint program).
    gui_header(x, y, w, T("panel.scenes"));
    GuiId add_id = gui_id("scene-add", 0);
    if (gui_button_id(add_id, "+", x + w - 22, y + 1, 18, 15, 0)) {
        static const char *const M[] = {"menu.new_scene_3d", "menu.new_scene_2d"};
        gui_menu_open_keys(add_id, M, 2);
    }
    int pick = gui_menu(add_id);
    if (pick >= 0) {
        Json *a = json_obj();
        json_sets(a, "kind", pick ? "2d" : "3d");
        json_sets(a, "name", pick ? "UI" : "Scene");
        ops_do("scene.create", a);
    }
    int ly = y + 20, lh = half - 20, row = 20;
    int shown = lh / row;
    scroll(&s_scroll_scenes, x, ly, w, lh, ed.nscenes, shown);
    gui_clip(x, ly, w - 1, lh);
    static GuiId menu_owner;
    static int menu_scene = -1;
    for (int k = s_scroll_scenes; k < ed.nscenes && k - s_scroll_scenes < shown; k++) {
        int i = ed.nscenes - 1 - k;
        Json *s = ed.scenes[i];
        int ry = ly + (k - s_scroll_scenes) * row;
        GuiId rid = gui_id(scene_id(s), 0x51);
        int vis = json_getb(s, "visible", 1), lock = json_getb(s, "locked", 0);
        // Eye / lock toggles first so they win over the row click.
        GuiId eid = gui_id(scene_id(s), 0xE7E), kid = gui_id(scene_id(s), 0x10C);
        int eo = gui_hover(eid, x + 4, ry + 2, 16, 16), ko = gui_hover(kid, x + 22, ry + 2, 16, 16);
        if (eo && gui.clicked) {
            Json *a = json_obj();
            json_sets(a, "scene", scene_id(s));
            Json *f = json_set(a, "fields", json_obj());
            json_setb(f, "visible", !vis);
            ops_do("scene.set", a);
            gui_take_mouse();
        }
        if (ko && gui.clicked) {
            Json *a = json_obj();
            json_sets(a, "scene", scene_id(s));
            Json *f = json_set(a, "fields", json_obj());
            json_setb(f, "locked", !lock);
            ops_do("scene.set", a);
            gui_take_mouse();
        }
        if (gui_row(rid, x + 1, ry, w - 5, row, i == ed.cur)) {
            if (i != ed.cur) { ed.cur = i; ed.sel[0] = ed.sel_note[0] = 0; }
            if (gui_dbl()) { s_rename_id = gui_id(scene_id(s), 0x2E4); snprintf(s_rename, sizeof(s_rename), "%s", json_gets(s, "name", "")); }
        }
        if (gui.hot == rid && gui.rclicked) {
            static const char *const M[] = {"menu.rename", "menu.move_up", "menu.move_down", "menu.copy_id", "-", "menu.delete"};
            menu_owner = gui_id("scene-menu", 0);
            menu_scene = i;
            gui_menu_open_keys(menu_owner, M, 6);
        }
        font_draw(x + 6, ry + 1, vis ? (eo ? C_BRIGHT : C_TEXT) : 0x3C3F45, vis ? "●" : "○");
        if (lock) gui_frame(x + 25, ry + 6, 8, 8, C_TEXT);
        else gui_frame(x + 25, ry + 6, 8, 8, ko ? C_DIM : C_EDGE);
        if (lock) gui_rect(x + 27, ry + 8, 4, 4, C_TEXT);
        kind_badge(x + 40, ry, scene_is_2d(s) ? "2D" : "3D", scene_is_2d(s) ? C_COLD : C_ACCENT);
        int open = 0;
        Json *notes = json_get(s, "notes");
        for (Json *n = notes ? notes->child : NULL; n; n = n->next) if (strcmp(json_gets(n, "status", "open"), "done")) open++;
        GuiId rn = gui_id(scene_id(s), 0x2E4);
        if (!rename_field(rn, x + 64, ry + 2, w - 72, "scene.set", scene_id(s), NULL, "name")) {
            gui_text_clip(x + 64, ry + 1, w - 72 - (open ? 26 : 0), i == ed.cur ? C_BRIGHT : (vis ? C_TEXT : C_DIM), json_gets(s, "name", scene_id(s)));
            if (open) { char t[8]; snprintf(t, sizeof(t), "%d", open); kind_badge(x + w - 30, ry, t, C_AGENT); }
        }
    }
    gui_unclip();
    if (menu_owner) {
        int m = gui_menu(menu_owner);
        Json *s = scene_at(menu_scene);
        if (m >= 0 && s) {
            if (m == 0) { s_rename_id = gui_id(scene_id(s), 0x2E4); snprintf(s_rename, sizeof(s_rename), "%s", json_gets(s, "name", "")); }
            if (m == 1 || m == 2) {
                Json *a = json_obj();
                json_sets(a, "scene", scene_id(s));
                json_setn(a, "index", menu_scene + (m == 1 ? 1 : -1));
                ops_do("scene.move", a);
            }
            if (m == 3) { input_clipboard_set(scene_id(s)); ed_status(C_OK, T("status.copied"), scene_id(s)); }
            if (m == 5) { Json *a = json_obj(); json_sets(a, "scene", scene_id(s)); ops_do("scene.delete", a); }
            menu_owner = 0;
        }
    }

    // Hierarchy of the active scene.
    int hy = y + half;
    Json *s = cur_scene();
    char title[160];
    snprintf(title, sizeof(title), "%s", T("panel.hierarchy"));
    gui_header(x, hy, w, title);
    GuiId oadd = gui_id("obj-add", 0);
    if (s && gui_button_id(oadd, "+", x + w - 22, hy + 1, 18, 15, 0)) {
        if (scene_is_2d(s)) gui_menu_open_keys(oadd, ADD_2D, 5);
        else gui_menu_open_keys(oadd, ADD_3D, 9);
    }
    int k = gui_menu(oadd);
    if (k >= 0) add_object(k);
    if (!s) return;
    Json *objs = scene_objects(s);
    int n = json_len(objs);
    int oy = hy + 20, oh = h - half - 20;
    shown = oh / 18;
    scroll(&s_scroll_objs, x, oy, w, oh, n, shown);
    gui_clip(x, oy, w - 1, oh);
    static GuiId omenu;
    static char omenu_id[64];
    int idx = 0;
    for (Json *o = objs->child; o; o = o->next, idx++) {
        if (idx < s_scroll_objs) continue;
        int ry = oy + (idx - s_scroll_objs) * 18;
        if (ry > oy + oh) break;
        const char *id = json_gets(o, "id", "");
        GuiId rid = gui_id(id, 0x0B1);
        if (gui_row(rid, x + 1, ry, w - 5, 18, !strcmp(id, ed.sel))) {
            snprintf(ed.sel, sizeof(ed.sel), "%s", id);
            ed.sel_note[0] = 0;
            if (gui_dbl()) view_frame_selected();
        }
        if (gui.hot == rid && gui.rclicked) {
            static const char *const M[] = {"menu.rename", "menu.duplicate", "menu.focus", "menu.copy_id", "-", "menu.delete"};
            omenu = gui_id("obj-menu", 0);
            snprintf(omenu_id, sizeof(omenu_id), "%s", id);
            gui_menu_open_keys(omenu, M, 6);
        }
        const char *t = json_gets(o, "type", "");
        font_draw(x + 8, ry, json_getb(o, "visible", 1) ? C_DIM : 0x3C3F45, t);
        GuiId rn = gui_id(id, 0x2E5);
        if (!rename_field(rn, x + 66, ry + 1, w - 74, "object.set", scene_id(s), id, "name"))
            gui_text_clip(x + 66, ry, w - 74, !strcmp(id, ed.sel) ? C_BRIGHT : C_TEXT, json_gets(o, "name", id));
    }
    gui_unclip();
    if (omenu) {
        int m = gui_menu(omenu);
        if (m >= 0) {
            Json *o = find_object(s, omenu_id);
            if (m == 0 && o) { s_rename_id = gui_id(omenu_id, 0x2E5); snprintf(s_rename, sizeof(s_rename), "%s", json_gets(o, "name", "")); }
            if (m == 1) { Json *a = args_scene(); json_sets(a, "id", omenu_id); ops_do("object.duplicate", a); }
            if (m == 2) { snprintf(ed.sel, sizeof(ed.sel), "%s", omenu_id); view_frame_selected(); }
            if (m == 3) { input_clipboard_set(omenu_id); ed_status(C_OK, T("status.copied"), omenu_id); }
            if (m == 5) { Json *a = args_scene(); json_sets(a, "id", omenu_id); ops_do("object.delete", a); }
            omenu = 0;
        }
    }
    if (n == 0) font_draw(x + 8, oy + 4, C_DIM, "");
}

// ---------------------------------------------------------------- right: inspector + notes
static void set_field(Json *s, Json *o, const char *key, Json *value)
{
    Json *a = json_obj();
    json_sets(a, "scene", scene_id(s));
    json_sets(a, "id", json_gets(o, "id", ""));
    Json *f = json_set(a, "fields", json_obj());
    json_set(f, key, value);
    ops_do("object.set", a);
}

static int vec_row(Json *s, Json *o, const char *key, const char *label, int x, int y, int w, int n, float step)
{
    float v[4] = {0, 0, 0, 0};
    json_getv(o, key, v, n);
    gui_text_clip(x, y, LABEL_W - 4, C_DIM, label);
    int fx = x + LABEL_W, fw = (w - LABEL_W - (n - 1) * 3) / n;
    static const unsigned int AX[4] = {0xC8584A, 0x7EB05E, 0x5A84C8, 0x9A9EA1};
    int changed = 0;
    for (int i = 0; i < n; i++) {
        gui_rect(fx + i * (fw + 3), y + 14, fw, 1, n == 3 ? AX[i] : AX[3]);
        if (gui_number(gui_id(json_gets(o, "id", ""), (int)gui_id(key, i)), fx + i * (fw + 3), y, fw, &v[i], step)) changed = 1;
    }
    if (changed) {
        // Number drags edit live; the field writes through one operation per frame.
        // A drag is one group: one undo step, one log line (ended when the mouse is up).
        if (gui.in.mouse_buttons & 1) ops_group_begin();
        Json *arr = json_arr();
        for (int i = 0; i < n; i++) json_push(arr, json_num(roundf(v[i] * 1000) / 1000));
        set_field(s, o, key, arr);
    }
    return 20;
}

static int text_row(GuiId id, const char *label, int x, int y, int w, char *buf, int size)
{
    gui_text_clip(x, y, LABEL_W - 4, C_DIM, label);
    return gui_field(id, x + LABEL_W, y, w - LABEL_W, buf, size);
}

static char s_model_names[22][64];
static int cb(const char *name, int is_dir, void *user)
{
    int *n = (int *)user;
    size_t l = strlen(name);
    if (is_dir || l < 5 || strcmp(name + l - 4, ".mcm") || *n >= 22) return 0;
    snprintf(s_model_names[*n], 64, "models/%s", name);
    (*n)++;
    return 0;
}

static void inspector_object(Json *s, Json *o, int x, int y, int w)
{
    int is2d = scene_is_2d(s);
    const char *id = json_gets(o, "id", "");
    char buf[256];
    font_drawf(x, y, C_DIM, "ID  %s", id);
    kind_badge(x + w - font_width(json_gets(o, "type", "")) - 8, y, json_gets(o, "type", ""), is2d ? C_COLD : C_ACCENT);
    y += 20;
    snprintf(buf, sizeof(buf), "%s", json_gets(o, "name", ""));
    if (text_row(gui_id(id, 0x4A3), T("field.name"), x, y, w, buf, sizeof(buf))) set_field(s, o, "name", json_str(buf));
    y += 20;
    if (is2d) {
        y += vec_row(s, o, "rect", T("field.rect"), x, y, w, 4, 1);
        const char *t = json_gets(o, "type", "");
        if (!strcmp(t, "text") || !strcmp(t, "button")) {
            snprintf(buf, sizeof(buf), "%s", json_gets(o, "text", ""));
            if (text_row(gui_id(id, 0x7E7), T("field.text"), x, y, w, buf, sizeof(buf))) set_field(s, o, "text", json_str(buf));
            y += 20;
        }
        if (!strcmp(t, "image")) {
            snprintf(buf, sizeof(buf), "%s", json_gets(o, "image", ""));
            if (text_row(gui_id(id, 0x1A6), T("field.image"), x, y, w, buf, sizeof(buf))) set_field(s, o, "image", json_str(buf));
            y += 20;
        }
    } else {
        y += vec_row(s, o, "pos", T("field.position"), x, y, w, 3, 0.1f);
        y += vec_row(s, o, "rot", T("field.rotation"), x, y, w, 3, 1.0f);
        y += vec_row(s, o, "scale", T("field.scale"), x, y, w, 3, 0.05f);
        if (!strcmp(json_gets(o, "type", ""), "model")) {
            snprintf(buf, sizeof(buf), "%s", json_gets(o, "model", ""));
            if (text_row(gui_id(id, 0x30D), T("field.model"), x, y, w - 22, buf, sizeof(buf))) set_field(s, o, "model", json_str(buf));
            GuiId mm = gui_id(id, 0x30E);
            if (gui_button_id(mm, "…", x + w - 20, y, 20, 16, 0)) {
                int n = 0;
                char dir[ED_PATH];
                file_path(dir, sizeof(dir), "models");
                plat_dir_list(dir, cb, &n);
                static const char *items[22];
                for (int i = 0; i < n; i++) items[i] = s_model_names[i];
                if (n) gui_menu_open(mm, items, n);
                else ed_status(C_WARN, "%s", T("status.no_models"));
            }
            int m = gui_menu(mm);
            if (m >= 0) set_field(s, o, "model", json_str(s_model_names[m]));
            y += 20;
        }
    }
    snprintf(buf, sizeof(buf), "%s", json_gets(o, "color", ""));
    unsigned int col = parse_color(buf, 0x808080);
    if (text_row(gui_id(id, 0xC01), T("field.color"), x, y, w - 22, buf, sizeof(buf))) set_field(s, o, "color", json_str(buf));
    gui_rect(x + w - 18, y, 18, 16, col);
    gui_frame(x + w - 18, y, 18, 16, C_EDGE);
    y += 20;
    int vis = json_getb(o, "visible", 1);
    if (gui_check("field.visible", x, y, &vis)) set_field(s, o, "visible", json_bool(vis));
    y += 20;
    // Free-form properties: anything a game or plugin wants to keep on the object.
    font_draw(x, y, C_DIM, T("field.props"));
    y += 18;
    Json *props = json_get(o, "props");
    char *t = props ? json_write(props, 0) : NULL;
    snprintf(buf, sizeof(buf), "%s", t ? t : "{}");
    free(t);
    if (gui_field(gui_id(id, 0x9A0), x, y, w, buf, sizeof(buf))) {
        char err[128];
        Json *p = json_parse(buf, err, sizeof(err));
        if (p && p->type == JSON_OBJ) set_field(s, o, "props", p);
        else { json_free(p); ed_status(C_WARN, T("status.props_invalid"), err); }
    }
    y += 20;
    snprintf(buf, sizeof(buf), "%s", json_gets(o, "script", ""));
    if (text_row(gui_id(id, 0x5C1), T("field.script"), x, y, w, buf, sizeof(buf))) set_field(s, o, "script", json_str(buf));
}

static void inspector_scene(Json *s, int x, int y, int w)
{
    char buf[256];
    const char *sid = scene_id(s);
    font_drawf(x, y, C_DIM, "ID  %s", sid);
    kind_badge(x + w - 24, y, scene_is_2d(s) ? "2D" : "3D", scene_is_2d(s) ? C_COLD : C_ACCENT);
    y += 20;
    snprintf(buf, sizeof(buf), "%s", json_gets(s, "name", ""));
    if (text_row(gui_id(sid, 0x4A4), T("field.name"), x, y, w, buf, sizeof(buf))) {
        Json *a = json_obj();
        json_sets(a, "scene", sid);
        Json *f = json_set(a, "fields", json_obj());
        json_sets(f, "name", buf);
        ops_do("scene.set", a);
    }
    y += 22;
    Json *set = json_get(s, "settings");
    if (scene_is_2d(s)) {
        float c[2] = {1280, 720};
        json_getv(set, "canvas", c, 2);
        font_draw(x, y, C_DIM, T("field.canvas"));
        int ch = 0;
        ch |= gui_number(gui_id(sid, 0xCA1), x + LABEL_W, y, (w - LABEL_W - 4) / 2, &c[0], 1);
        ch |= gui_number(gui_id(sid, 0xCA2), x + LABEL_W + (w - LABEL_W) / 2, y, (w - LABEL_W - 4) / 2, &c[1], 1);
        if (ch) {
            Json *a = json_obj();
            json_sets(a, "scene", sid);
            Json *f = json_set(a, "fields", json_obj());
            Json *st = json_set(f, "settings", json_obj());
            json_setv(st, "canvas", c, 2);
            ops_do("scene.set", a);
        }
        y += 22;
    } else {
        float fog[2] = {20, 80};
        json_getv(set, "fog", fog, 2);
        font_draw(x, y, C_DIM, T("field.fog_range"));
        int ch = gui_number(gui_id(sid, 0xF01), x + LABEL_W, y, (w - LABEL_W - 4) / 2, &fog[0], 1);
        ch |= gui_number(gui_id(sid, 0xF02), x + LABEL_W + (w - LABEL_W) / 2, y, (w - LABEL_W - 4) / 2, &fog[1], 1);
        snprintf(buf, sizeof(buf), "%s", json_gets(set, "fog_color", "#34383C"));
        y += 20;
        int cc = text_row(gui_id(sid, 0xF03), T("field.fog_color"), x, y, w - 22, buf, sizeof(buf));
        gui_rect(x + w - 18, y, 18, 16, parse_color(buf, 0x34383C));
        y += 20;
        int prev = json_getb(set, "fog_enabled", 0);
        int pc = gui_check("field.fog_enabled", x, y, &prev);
        if (ch || cc || pc) {
            Json *a = json_obj();
            json_sets(a, "scene", sid);
            Json *f = json_set(a, "fields", json_obj());
            Json *st = json_set(f, "settings", json_obj());
            json_setv(st, "fog", fog, 2);
            json_sets(st, "fog_color", buf);
            json_setb(st, "fog_enabled", prev);
            ops_do("scene.set", a);
        }
        y += 22;
    }
    font_draw(x, y, C_DIM, T("field.ports"));
    y += 18;
    Json *ports = json_get(s, "ports");
    for (Json *p = ports ? ports->child : NULL; p; p = p->next) {
        const char *dir = json_gets(p, "dir", "in");
        font_drawf(x + 6, y, !strcmp(dir, "in") ? C_COLD : C_ACCENT, "%s %s", !strcmp(dir, "in") ? T("port.in") : T("port.out"), json_gets(p, "name", ""));
        y += 17;
    }
    font_draw(x, y + 4, C_DIM, "");
}

static void notes_list(Json *s, int x, int y, int w, int h)
{
    Json *notes = scene_notes(s);
    int total = json_len(notes), open = 0;
    for (Json *n = notes->child; n; n = n->next) if (strcmp(json_gets(n, "status", "open"), "done")) open++;
    char title[64];
    snprintf(title, sizeof(title), T("panel.notes"), open, total);
    gui_header(x, y, w, title);
    y += 20;
    h -= 20;
    Json *sel = find_note(s, ed.sel_note);
    int detail = sel ? 118 : 0;
    int lh = h - detail, shown = lh / 18;
    scroll(&s_scroll_notes, x, y, w, lh, total, shown);
    gui_clip(x, y, w - 1, lh);
    int k = 0;
    for (Json *n = notes->child; n; n = n->next, k++) {
        if (k < s_scroll_notes) continue;
        int ry = y + (k - s_scroll_notes) * 18;
        if (ry > y + lh) break;
        const char *id = json_gets(n, "id", "");
        int done = !strcmp(json_gets(n, "status", "open"), "done");
        if (gui_row(gui_id(id, 0x707), x + 1, ry, w - 5, 18, !strcmp(id, ed.sel_note))) {
            snprintf(ed.sel_note, sizeof(ed.sel_note), "%s", id);
            if (!scene_is_2d(s) && json_get(n, "camera")) cam_from_json(&ed.cams[ed.cur], json_get(n, "camera"));
        }
        unsigned int c = done ? C_DIM : parse_color(json_gets(n, "color", NULL), C_NOTE);
        const char *author = json_gets(n, "author", "?");
        int agent = strcmp(author, "human") != 0;
        font_drawf(x + 6, ry, agent && !done ? C_AGENT : c, "%d", k + 1);
        gui_text_clip(x + 24, ry, w - 32, done ? C_DIM : C_TEXT, json_gets(n, "text", ""));
    }
    gui_unclip();
    if (!total) font_draw(x + 8, y + 4, C_DIM, "");
    if (!sel) return;
    int dy = y + lh + 2;
    gui_rect(x, dy - 2, w, 1, C_EDGE_IN);
    int done = !strcmp(json_gets(sel, "status", "open"), "done");
    font_drawf(x + 4, dy, C_DIM, "%s · %s", json_gets(sel, "author", "?"), json_gets(sel, "created", ""));
    dy += 17;
    Json *tg = json_get(sel, "targets");
    char line[200];
    snprintf(line, sizeof(line), "%s ", T("note.targets"));
    for (Json *t = tg ? tg->child : NULL; t; t = t->next) {
        if (strlen(line) + strlen(t->str) + 2 >= sizeof(line)) break;
        strcat(line, t->str);
        if (t->next) strcat(line, ", ");
    }
    if (!json_len(tg)) strcat(line, "—");
    gui_text_clip(x + 4, dy, w - 8, C_TEXT, line);
    dy += 17;
    Json *rep = json_get(sel, "replies");
    if (json_len(rep)) {
        Json *last = json_at(rep, json_len(rep) - 1);
        char t[300];
        snprintf(t, sizeof(t), "%s: %s", json_gets(last, "author", "?"), json_gets(last, "text", ""));
        gui_text_clip(x + 4, dy, w - 8, C_AGENT, t);
    }
    dy += 18;
    if (gui_field(gui_id("reply", 0), x + 4, dy, w - 8, s_reply, sizeof(s_reply)) && s_reply[0]) {
        Json *a = args_scene();
        json_sets(a, "id", ed.sel_note);
        json_sets(a, "text", s_reply);
        ops_do("note.reply", a);
        s_reply[0] = 0;
    }
    if (!gui_focused(gui_id("reply", 0)) && !s_reply[0]) font_draw(x + 8, dy, 0x5A5E66, T("note.reply"));
    dy += 21;
    int bw = (w - 16) / 3;
    if (gui_button(done ? "note.reopen" : "note.resolve", x + 4, dy, bw, 18)) {
        Json *a = args_scene();
        json_sets(a, "id", ed.sel_note);
        if (done) json_setb(a, "reopen", 1);
        ops_do("note.resolve", a);
    }
    if (gui_button("menu.copy_id", x + 8 + bw, dy, bw, 18)) { input_clipboard_set(ed.sel_note); ed_status(C_OK, T("status.copied"), ed.sel_note); }
    if (gui_button("menu.delete", x + 12 + bw * 2, dy, bw, 18)) {
        Json *a = args_scene();
        json_sets(a, "id", ed.sel_note);
        ops_do("note.delete", a);
        ed.sel_note[0] = 0;
    }
}

void panels_right(int x, int y, int w, int h)
{
    gui_rect(x, y, w, h, C_PANEL);
    gui_rect(x, y, 1, h, C_EDGE);
    Json *s = cur_scene();
    int top = h * 11 / 20;
    gui_header(x, y, w, T("panel.inspector"));
    if (!s) return;
    Json *o = find_object(s, ed.sel);
    if (o) inspector_object(s, o, x + 8, y + 24, w - 16);
    else inspector_scene(s, x + 8, y + 24, w - 16);
    notes_list(s, x, y + top, w, h - top);
}

// ---------------------------------------------------------------- bottom: console
static const char *const TABS[CH_COUNT] = {"console.log", "console.ops", "console.build", "console.lua"};

void panel_bottom(int x, int y, int w, int h)
{
    gui_rect(x, y, w, h, C_PANEL);
    gui_rect(x, y, w, 1, C_EDGE);
    for (int i = 0; i < CH_COUNT; i++)
        if (gui_tab(TABS[i], x + 4 + i * 86, y + 2, 84, ed.bottom_tab == i)) ed.bottom_tab = i;
    int ch = ed.bottom_tab;
    int ly = y + 22, lh = h - 24 - (ch == CH_LUA ? 20 : 0);
    int n = log_count(ch), rows = lh / 16;
    int *sc = &s_scroll_log[ch];
    // Scroll value counts lines from the bottom.
    if (gui_hit(x, ly, w, lh) && gui.in.wheel && !gui.wheel_used) { *sc += gui.in.wheel * 2; gui.wheel_used = 1; }
    if (*sc > n - rows) *sc = n - rows;
    if (*sc < 0) *sc = 0;
    gui_clip(x, ly, w, lh);
    int first = n - rows - *sc;
    if (first < 0) first = 0;
    for (int i = first, r = 0; i < n && r < rows; i++, r++) {
        int kind;
        const char *t = log_line(ch, i, &kind);
        static const unsigned int COL[] = {C_TEXT, C_OK, C_ACCENT, C_WARN, C_AGENT, C_TEXT, C_COLD};
        unsigned int c = COL[kind < 7 ? kind : 0];
        if (ch == CH_BUILD) {
            if (strstr(t, "error")) c = C_WARN;
            else if (strstr(t, "warning")) c = C_ACCENT;
            else if (strstr(t, "OK") || strstr(t, "done")) c = C_OK;
        }
        gui_text_clip(x + 6, ly + r * 16 - 1, w - 12, c, t);
    }
    gui_unclip();
    if (!n) font_draw(x + 8, ly + 2, C_DIM, ch == CH_AGENT ? "" : "");
    if (ch == CH_LUA) {
        GuiId id = gui_id("lua-input", 0);
        font_draw(x + 6, y + h - 19, C_COLD, ">");
        if (gui_field(id, x + 18, y + h - 20, w - 24, s_lua, sizeof(s_lua)) && s_lua[0]) {
            log_to(CH_LUA, LOG_LUA, s_lua);
            Json *r = script_run(s_lua, "console");
            const char *out = json_gets(r, "output", "");
            if (out[0]) {
                char line[240];
                const char *p = out;
                while (*p) {
                    int k = 0;
                    while (p[k] && p[k] != '\n' && k < 230) k++;
                    snprintf(line, sizeof(line), "%.*s", k, p);
                    log_to(CH_LUA, LOG_INFO, line);
                    p += k;
                    if (*p == '\n') p++;
                }
            }
            if (!json_getb(r, "ok", 0)) log_to(CH_LUA, LOG_ERR, json_gets(r, "error", "error"));
            json_free(r);
            s_lua[0] = 0;
            gui_focus(id, "");
        }
        if (!gui_focused(id) && !s_lua[0]) font_draw(x + 22, y + h - 20, 0x5A5E66, "Lua");
    }
}
