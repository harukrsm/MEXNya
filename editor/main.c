// MEXNya Editor entry point.
//
//   MexnyaEditor [--project DIR]                 the GUI (what `mexnya run` starts)
//   MexnyaEditor --project DIR --exec '<json>'   one operation, headless, JSON to stdout
//   MexnyaEditor --project DIR --script f.lua    run a Lua script after loading (tests)
//   options: --as NAME (who the log credits), --backend software|gl|auto,
//            --frames N and --shot out.png (render N frames, save the window, quit)
#include "ed.h"
#include "gui.h"
#include "engine/engine.h"
#include "engine/core/png.h"
#include "engine/platform/platform.h"
#include "engine/platform/file.h"
#include "engine/render/font.h"
#include "engine/core/i18n.h"
#include "engine/render/gfx.h"
#include "engine/render/render_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

extern char ed_shot_path[];
static int s_lock = -1;
static void settings_dialog(void);
static const char *const MODES[MODE_COUNT] = {"tab.scene", "tab.graph", "tab.build", "tab.git", "tab.agent", "tab.plugins"};

static int dir_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && (st.st_mode & S_IFDIR);
}

static void find_root(char *out, int size, const char *given)
{
    if (given) { snprintf(out, (size_t)size, "%s", given); return; }
    char d[ED_PATH];
    if (!getcwd(d, sizeof(d))) snprintf(d, sizeof(d), ".");
    char probe[ED_PATH + 16];
    for (;;) {
        snprintf(probe, sizeof(probe), "%s/mexnya.json", d);
        if (plat_file_exists(probe)) { snprintf(out, (size_t)size, "%s", d); return; }
        char *slash = strrchr(d, '/');
        if (!slash || slash == d) break;
        *slash = 0;
    }
    if (!getcwd(out, (size_t)size)) snprintf(out, (size_t)size, ".");
}

static int load_font(void)
{
    char p[ED_PATH + 64];
    const char *rel[] = {"engine-resources/fonts/mx12.mxf", "../Resources/engine-resources/fonts/mx12.mxf"};
    for (int i = 0; i < 2; i++) {
        snprintf(p, sizeof(p), "%s/%s", ed.exe_dir, rel[i]);
        if (font_load(p) == 0) return 0;
    }
    snprintf(p, sizeof(p), "%s/engine/resources/fonts/mx12.mxf", ed.root);
    return font_load(p);
}

// ---------------------------------------------------------------- chrome
static void top_bar(void)
{
    gui_rect(0, 0, ED_W, 20, 0x1B1C1F);
    gui_rect(0, 19, ED_W, 1, C_EDGE);
    // Widths follow the text, so every language fits.
    int lx = 8;
    lx += font_draw(lx, 2, C_ACCENT, "MEXNya") + 6;
    lx += font_draw(lx, 2, C_DIM, "editor") + 12;
    gui_text_clip(lx, 2, 140, C_TEXT, json_gets(ed.project, "name", ""));
    int x = lx + 150;
    for (int i = 0; i < MODE_COUNT; i++) {
        int tw = font_width(T(MODES[i])) + 24;
        if (tw < 64) tw = 64;
        if (gui_tab(MODES[i], x, 1, tw, ed.mode == i)) ed.mode = i;
        x += tw + 2;
    }
    int rx = ED_W - 8;
    const char *saved = ed.dirty ? T("top.unsaved") : "";
    rx -= font_width(saved);
    font_draw(rx, 2, ed.dirty ? C_ACCENT : C_DIM, saved);
    rx -= 6;
    static const char *const KEYS[4] = {"top.redo", "top.undo", "top.settings", "top.save"};
    for (int i = 0; i < 4; i++) {
        int bw = font_width(T(KEYS[i])) + 16;
        if (bw < 44) bw = 44;
        rx -= bw;
        int on = i == 2 && ed.settings_open;
        if (!gui_button_id(gui_id(KEYS[i], 0x70B), T(KEYS[i]), rx, 2, bw, 16, on)) { rx -= 4; continue; }
        rx -= 4;
        if (i == 0 && ops_can_redo()) ops_redo();
        if (i == 1 && ops_can_undo()) ops_undo();
        if (i == 2) ed.settings_open = !ed.settings_open;
        if (i == 3 && project_save() == 0) ed_status(C_OK, "%s", T("status.saved"));
    }
}

static void status_bar(void)
{
    int y = ED_H - 16;
    gui_rect(0, y, ED_W, 16, 0x1B1C1F);
    gui_rect(0, y, ED_W, 1, C_EDGE);
    if (ed.status[0] && ed.time - ed.status_t < 8.0) gui_text_clip(8, y - 1, ED_W - 260, ed.status_rgb, ed.status);

    char r[96];
    int pw = 0, ph = 0;
    r_size(&pw, &ph);
    snprintf(r, sizeof(r), "%s   %d×%d   %d×", r_backend_name(), pw, ph, ed.ui_scale);
    font_draw(ED_W - 8 - font_width(r), y - 1, C_DIM, r);
}

static void shortcuts(void)
{
    if (gui.focus) return;
    int sc = input_shortcut_mod();
    if (sc && input_key_pressed(KEY_LETTER('S'))) { if (project_save() == 0) ed_status(C_OK, "%s", T("status.saved")); }
    if (sc && input_key_pressed(KEY_LETTER('Z'))) { if (input_mods() & MOD_SHIFT) ops_redo(); else ops_undo(); }
    if (sc && input_key_pressed(KEY_LETTER('Y'))) ops_redo();
    if (sc && (input_key_pressed(KEY_EQUALS) || input_key_pressed(KEY_MINUS))) {
        int k = ed.ui_scale + (input_key_pressed(KEY_EQUALS) ? 1 : -1);
        if (k >= 1 && k <= 4) { ed.ui_scale = k; ed_status(C_DIM, T("status.ui_scale"), k); }
    }
    for (int i = 0; i < MODE_COUNT; i++)
        if (sc && input_key_pressed(KEY_1 + i)) ed.mode = i;
    if (input_key_pressed(KEY_F5)) {
        Json *r = ops_call("project.reload", NULL, "human");
        ed_status(json_getb(r, "ok", 0) ? C_OK : C_WARN, "%s", json_getb(r, "ok", 0) ? T("status.reloaded") : json_gets(r, "error", ""));
        json_free(r);
    }
    if (ed.mode != MODE_SCENE) return;
    Json *s = cur_scene();
    if (!s) return;
    if ((input_key_pressed(KEY_DELETE) || input_key_pressed(KEY_BACKSPACE)) && !notes_editing()) {
        Json *a = json_obj();
        json_sets(a, "scene", scene_id(s));
        if (ed.sel_note[0]) { json_sets(a, "id", ed.sel_note); ops_do("note.delete", a); ed.sel_note[0] = 0; }
        else if (ed.sel[0]) { json_sets(a, "id", ed.sel); ops_do("object.delete", a); }
        else json_free(a);
    }
    if (sc && input_key_pressed(KEY_LETTER('D')) && ed.sel[0]) {
        Json *a = json_obj();
        json_sets(a, "scene", scene_id(s));
        json_sets(a, "id", ed.sel);
        ops_do("object.duplicate", a);
    }
    if (!sc && !(gui.in.mouse_buttons & 2) && input_key_pressed(KEY_LETTER('F'))) view_frame_selected();
    if (input_key_pressed(KEY_ESCAPE) && !notes_editing()) { ed.sel[0] = ed.sel_note[0] = 0; }
    // Numpad views like Blender/Unity: 7 top, 1 front, 3 right, 5 toggles perspective.
    static const struct { int key; const char *preset; } NP[] = {{KEY_KP_7, "top"}, {KEY_KP_1, "front"}, {KEY_KP_3, "right"}};
    for (int i = 0; i < 3; i++)
        if (input_key_pressed(NP[i].key)) { Json *a = json_obj(); json_sets(a, "preset", NP[i].preset); ops_do("view.set", a); }
    if (input_key_pressed(KEY_KP_5)) { ed.cams[ed.cur].ortho = !ed.cams[ed.cur].ortho; ed.dirty = 1; }
}

static void frame_ui(void)
{
    gui_begin(&ed.in, ed.time);
    input_set_cursor(0);
    gui_rect(0, 0, ED_W, ED_H, C_BG);
    int top = 20, bottom = ED_H - 16;
    if (ed.mode == MODE_SCENE) {
        int tw = 30, lw = 186, rw = 220, bh = 118;
        int vx = tw + lw, vw = ED_W - vx - rw, vh = bottom - top - bh;
        // The viewport first: panels drawn after it win the mouse where they overlap.
        view3d_frame(vx, top, vw, vh);
        toolbar(0, top, tw, bottom - top);
        panels_left(tw, top, lw, bottom - top);
        panels_right(ED_W - rw, top, rw, bottom - top);
        panel_bottom(vx, top + vh, vw, bh);
        notes_popup();
    } else if (ed.mode == MODE_GRAPH) graph_frame(0, top, ED_W, bottom - top);
    else if (ed.mode == MODE_BUILD) build_page(0, top, ED_W, bottom - top);
    else if (ed.mode == MODE_GIT) git_page(0, top, ED_W, bottom - top);
    else if (ed.mode == MODE_AGENT) agent_page(0, top, ED_W, bottom - top);
    else plugins_page(0, top, ED_W, bottom - top);
    top_bar();
    status_bar();
    if (!ed.settings_open) shortcuts();
    settings_dialog();
    gui_end();
    if (!(gui.in.mouse_buttons & 7) && ops_in_group()) ops_group_end();
}

static void save_window(const char *path)
{
    int w = 0, h = 0;
    r_size(&w, &h);
    unsigned char *rgb = (unsigned char *)malloc((size_t)w * (size_t)h * 3);
    if (!rgb) return;
    r_copy_frame_rgb(rgb);
    png_save_rgb(path, rgb, w, h);
    free(rgb);
}

// ---------------------------------------------------------------- preferences
static void prefs_path(char *out, int size)
{
    char dir[ED_PATH];
    plat_user_dir(dir, sizeof(dir));
    snprintf(out, (size_t)size, "%s/editor.json", dir);
}

static void prefs_load(void)
{
    char p[ED_PATH + 16];
    prefs_path(p, sizeof(p));
    Json *j = json_load(p, NULL, 0);
    int s = (int)json_getn(j, "ui_scale", 0);
    const char *lang = json_gets(j, "language", NULL);
    if (lang && i18n_lang_index(lang) >= 0) snprintf(ed.language, sizeof(ed.language), "%s", lang);
    const char *font = json_gets(j, "font", NULL);
    if (font) ed.font_pref = !strcmp(font, "system") ? FONT_SYSTEM : FONT_PIXEL;
    json_free(j);
    if (s >= 1 && s <= 4) ed.ui_scale = s;
}

static void prefs_save(void)
{
    char p[ED_PATH + 16];
    prefs_path(p, sizeof(p));
    Json *j = json_obj();
    json_setn(j, "ui_scale", ed.ui_scale);
    json_sets(j, "language", ed.language);
    json_sets(j, "font", ed.font_pref == FONT_SYSTEM ? "system" : "pixel");
    json_save(p, j);
    json_free(j);
}

static void apply_ui_scale(void)
{
    r_set_ui_scale(ed.ui_scale);
    r_ui_size(&ed.w, &ed.h);
}

// Language and font are presentation only: they change what is drawn, never what is stored.
static void apply_language_and_font(void)
{
    static char lang[16];
    static int font = -1, scale = -1;
    if (strcmp(lang, ed.language)) {
        i18n_set_language(ed.language);
        snprintf(ed.language, sizeof(ed.language), "%s", i18n_language());
    }
    if (strcmp(lang, ed.language) || font != ed.font_pref || scale != ed.ui_scale) {
        font_set_mode(ed.font_pref, ed.language, ed.ui_scale);
        snprintf(lang, sizeof(lang), "%s", ed.language);
        font = ed.font_pref;
        scale = ed.ui_scale;
    }
}

static int load_tables(void)
{
    char p[ED_PATH + 64];
    const char *rel[] = {"engine-resources/lang", "../Resources/engine-resources/lang"};
    for (int i = 0; i < 2; i++) {
        snprintf(p, sizeof(p), "%s/%s", ed.exe_dir, rel[i]);
        if (i18n_add_domain(p, "editor") > 0) return 0;
    }
    snprintf(p, sizeof(p), "%s/engine/resources/lang", ed.root);
    return i18n_add_domain(p, "editor") > 0 ? 0 : -1;
}

// ---------------------------------------------------------------- settings
static void settings_dialog(void)
{
    if (!ed.settings_open) return;
    int w = 420, h = 236, x = (ED_W - w) / 2, y = (ED_H - h) / 3;
    gui_rect_a(0, 0, ED_W, ED_H, 0x000000, 110);
    gui_panel(x, y, w, h);
    gui_header(x + 1, y + 1, w - 2, T("settings.title"));
    int cy = y + 32, lx = x + 14, cx = x + 120, cw = w - 134;
    font_draw(lx, cy + 2, C_DIM, T("settings.language"));
    for (int i = 0; i < I18N_LANG_COUNT; i++) {
        int col = i % 3, row = i / 3, bw = (cw - 8) / 3;
        // Language names are shown in their own language and are never translated.
        if (gui_button_id(gui_id("lang", i), i18n_lang_name(i), cx + col * (bw + 4), cy + row * 24, bw, 20,
                          !strcmp(ed.language, i18n_lang_code(i))))
            snprintf(ed.language, sizeof(ed.language), "%s", i18n_lang_code(i));
    }
    cy += 58;
    font_draw(lx, cy + 2, C_DIM, T("settings.font"));
    int bw = (cw - 4) / 2;
    if (gui_button_id(gui_id("font", 0), T("settings.font_pixel"), cx, cy, bw, 20, ed.font_pref == FONT_PIXEL)) ed.font_pref = FONT_PIXEL;
    if (gui_button_id(gui_id("font", 1), T("settings.font_system"), cx + bw + 4, cy, bw, 20, ed.font_pref == FONT_SYSTEM)) ed.font_pref = FONT_SYSTEM;
    if (ed.font_pref == FONT_SYSTEM && font_mode() != FONT_SYSTEM) font_draw(cx, cy + 24, C_WARN, T("settings.font_unavailable"));
    cy += 46;
    font_draw(lx, cy + 2, C_DIM, T("settings.ui_scale"));
    for (int k = 1; k <= 4; k++) {
        char t[8];
        snprintf(t, sizeof(t), "%d×", k);
        int sw = (cw - 12) / 4;
        if (gui_button_id(gui_id("scale", k), t, cx + (k - 1) * (sw + 4), cy, sw, 20, ed.ui_scale == k)) ed.ui_scale = k;
    }
    if (gui_button("settings.close", x + w - 104, y + h - 30, 90, 20) || (input_key_pressed(KEY_ESCAPE) && !gui.focus)) {
        ed.settings_open = 0;
        prefs_save();
    }
    gui_take_mouse();
}

// Input arrives in frame pixels; the editor works in UI points.
static void input_to_points(Input *in)
{
    int k = ed.ui_scale;
    if (k <= 1) return;
    in->mouse_x /= k;
    in->mouse_y /= k;
    in->mouse_dx /= (float)k;
    in->mouse_dy /= (float)k;
}

// ---------------------------------------------------------------- headless
static int run_exec(const char *json, const char *who)
{
    char err[256];
    Json *req = json_parse(json, err, sizeof(err));
    if (!req) {
        printf("{\"ok\": false, \"error\": \"bad request JSON: %s\"}\n", err);
        return 2;
    }
    Json *r;
    Json *batch = json_get(req, "batch");
    if (batch && batch->type == JSON_ARR) {
        r = json_obj();
        json_setb(r, "ok", 1);
        Json *res = json_set(r, "results", json_arr());
        for (Json *q = batch->child; q; q = q->next) {
            Json *one = ops_exec(q, json_gets(req, "agent", who));
            if (!json_getb(one, "ok", 0)) json_setb(r, "ok", 0);
            json_push(res, one);
        }
    } else r = ops_exec(req, json_gets(req, "agent", who));
    if (ed.dirty) project_save();
    char *t = json_write(r, 2);
    fputs(t ? t : "{}", stdout);
    free(t);
    int good = json_getb(r, "ok", 0);
    json_free(r);
    json_free(req);
    return good ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *project = NULL, *exec = NULL, *script = NULL, *shot = NULL, *backend = "gl", *who = NULL;
    int frames = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--project") && i + 1 < argc) project = argv[++i];
        else if (!strcmp(argv[i], "--exec") && i + 1 < argc) exec = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--backend") && i + 1 < argc) backend = argv[++i];
        else if (!strcmp(argv[i], "--as") && i + 1 < argc) who = argv[++i];
        else if (!strcmp(argv[i], "--headless")) ed.headless = 1;
        else if (!strcmp(argv[i], "--help")) { puts("MexnyaEditor [--project DIR] [--exec JSON] [--script FILE] [--as NAME]"); return 0; }
    }
    if (exec) ed.headless = 1;
    file_set_game_dir(argv[0]);
    plat_exe_dir(ed.exe_dir, sizeof(ed.exe_dir));
    find_root(ed.root, sizeof(ed.root), project);
    snprintf(ed.agent_name, sizeof(ed.agent_name), "%s", who && !exec ? who : "human");

    char lock_dir[ED_PATH + 16], lock_path[ED_PATH + 32];
    snprintf(lock_dir, sizeof(lock_dir), "%s/mexnya", ed.root);
    plat_mkdirs(lock_dir);
    snprintf(lock_path, sizeof(lock_path), "%s/.lock", lock_dir);
    s_lock = plat_try_lock(lock_path);
    if (s_lock < 0) {
        if (exec)
            printf("{\"ok\": false, \"error\": \"another MEXNya editor instance is using this project\"}\n");
        else
            fprintf(stderr, "MexnyaEditor: another editor instance is using this project (%s)\n", lock_path);
        return 1;
    }
    char assets[ED_PATH + 16];
    snprintf(assets, sizeof(assets), "%s/assets", ed.root);
    if (dir_exists(assets)) file_set_data_dir(assets);

    // The GUI runs at the window's native resolution; headless runs render offscreen.
    EngineConfig cfg = {1600, 1000, 0, ed.headless, "MEXNya Editor", "MEXNya", "Editor",
                        ed.headless ? "software" : backend, !ed.headless};
    if (engine_init(&cfg) != 0) {
        fprintf(stderr, "MexnyaEditor: engine init failed\n");
        plat_unlock(s_lock);
        return 1;
    }
    ed.ui_scale = 1;
    snprintf(ed.language, sizeof(ed.language), "%s", i18n_system_language());
    ed.font_pref = FONT_PIXEL;
    if (load_tables() != 0) fprintf(stderr, "MexnyaEditor: UI text tables missing (engine/resources/lang)\n");
    if (!ed.headless) {
        float d = plat_video_density();
        ed.ui_scale = d >= 1.75f ? (int)(d + 0.25f) : 1;
        prefs_load();
    }
    apply_ui_scale();
    if (ed.headless) snprintf(ed.language, sizeof(ed.language), "en");
    if (load_font() != 0) fprintf(stderr, "MexnyaEditor: font engine/resources/fonts/mx12.mxf missing (run engine/tools/gen_font.py)\n");
    tools_init();
    script_init();
    if (project_open(ed.root) != 0) {
        fprintf(stderr, "MexnyaEditor: cannot open the project in %s/mexnya\n", ed.root);
        engine_shutdown();
        plat_unlock(s_lock);
        return 1;
    }
    plugins_load();
    agent_init();
    if (exec) {
        int r = run_exec(exec, who ? who : (getenv("MEXNYA_AGENT") ? getenv("MEXNYA_AGENT") : "agent"));
        script_shutdown();
        project_close();
        engine_shutdown();
        plat_unlock(s_lock);
        return r;
    }
    ed.time = plat_time();
    ed_log(LOG_OK, "MEXNya Editor %s · %s · %s", ED_VERSION, json_gets(ed.project, "name", ""), r_backend_name());
    if (script) {
        int size = 0;
        char *buf = (char *)plat_read_file(script, &size, 0);
        if (buf) {
            char *code = (char *)malloc((size_t)size + 1);
            memcpy(code, buf, (size_t)size);
            code[size] = 0;
            free(buf);
            script_start(code, script);  // runs across frames (mx.wait)
            free(code);
        } else fprintf(stderr, "script: cannot read %s\n", script);
    }
    double last = plat_time(), save_t = 0, disk_t = 0, active_t = 0, drawn_t = 0;
    int frame = 0;
    while (engine_running() && !ed.quit) {
        double now = plat_time();
        ed.dt = (float)(now - last);
        if (ed.dt > 0.1f) ed.dt = 0.1f;
        last = now;
        ed.time = now;
        script_tick();
        input_poll(&ed.in);
        input_to_points(&ed.in);
        snapshots_process();
        int agent_ops = agent_update();
        tools_update();
        // Redraw while something happens; idle, a few frames a second are enough.
        if (ed.in.mouse_moved || ed.in.mouse_buttons || ed.in.wheel || ed.in.held || ed.in.pressed ||
            input_text_typed()[0] || agent_ops || job_running() || script_running() || gui.focus || frames > 0)
            active_t = now;
        if (now - active_t > 0.6 && now - drawn_t < 0.25 && !ed_shot_path[0]) {
            plat_sleep_ms(8);
            continue;
        }
        drawn_t = now;
        // Autosave so agents reading the files see what the person did.
        if (ed.dirty && now - save_t > 1.5 && !(ed.in.mouse_buttons & 7) && !gui.focus) { project_save(); save_t = now; }
        if (!ed.dirty && now - disk_t > 2.0) {
            disk_t = now;
            if (project_changed_on_disk()) {
                Json *r = ops_call("project.reload", NULL, "disk");
                ed_status(C_COLD, "%s", T("status.reloaded_external"));
                json_free(r);
            }
        }
        gfx_begin_3d();
        apply_ui_scale();
        apply_language_and_font();
        gfx_begin_2d();
        ed_in_frame = 1;
        frame_ui();
        ed_in_frame = 0;
        gfx_finish();
        frame++;
                if (ed_shot_path[0]) {
            save_window(ed_shot_path);
            ed_shot_path[0] = 0;
        }
        if (shot && frames > 0 && frame == frames) save_window(shot);
        gfx_present(ed.dt * 1000.0f);
        if (frames > 0 && frame >= frames) break;
        if (script && !script_running() && frames == 0 && getenv("MEXNYA_SCRIPT_QUIT")) break;
    }
    if (ed.dirty) project_save();
    if (!ed.headless) prefs_save();
    agent_shutdown_beat();
    script_shutdown();
    project_close();
    engine_shutdown();
    plat_unlock(s_lock);
    return 0;
}
