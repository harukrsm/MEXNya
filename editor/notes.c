// Notes (annotations): box, circle, arrow, pin and pen marks drawn straight onto a
// scene, each with text. A note records exactly what was marked so an agent can act
// on it without guessing: the shape in pixels (canvas pixels for 2D scenes, viewport
// pixels at the saved camera for 3D), the objects inside it, the world point under
// its centre, and a PNG snapshot with the mark drawn in.
#include "ed.h"
#include "gui.h"
#include "engine/core/png.h"
#include "engine/platform/platform.h"
#include "engine/render/font.h"
#include "engine/render/gfx.h"
#include "engine/render/render_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ed_in_frame;

// ---------------------------------------------------------------- shape helpers
typedef struct {
    int kind;                 // TOOL_BOX..TOOL_PEN
    float a[2], b[2];         // drag start/end in shape space
    float pts[256][2];
    int npts;
} Draft;

static Draft s_draft;
static int s_drawing;         // mouse held, shape growing
static int s_typing;          // shape done, text popup open
static char s_text[512];
static int s_scene_index = -1;
static Cam s_cam;
static int s_vw, s_vh;

int notes_editing(void) { return s_drawing || s_typing; }

static const char *kind_name(int tool)
{
    switch (tool) {
    case TOOL_CIRCLE: return "circle";
    case TOOL_ARROW: return "arrow";
    case TOOL_PIN: return "pin";
    case TOOL_PEN: return "pen";
    default: return "box";
    }
}

// Shape space -> screen. 2D: canvas pixels. 3D: viewport pixels at the note's view size.
static void to_screen(const View *v, int is2d, const float *vs, float x, float y, float *sx, float *sy)
{
    if (is2d) { *sx = v->cx + x * v->cscale; *sy = v->cy + y * v->cscale; return; }
    float kx = vs && vs[0] > 0 ? v->w / vs[0] : 1, ky = vs && vs[1] > 0 ? v->h / vs[1] : 1;
    *sx = v->x + x * kx;
    *sy = v->y + y * ky;
}

static void from_screen(const View *v, int is2d, float sx, float sy, float *x, float *y)
{
    if (is2d) { *x = roundf((sx - v->cx) / v->cscale); *y = roundf((sy - v->cy) / v->cscale); return; }
    *x = roundf(sx - v->x);
    *y = roundf(sy - v->y);
}

static void arrow(float x0, float y0, float x1, float y1, unsigned int c)
{
    gui_line(x0, y0, x1, y1, c);
    gui_line(x0 + 1, y0, x1 + 1, y1, c);
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1) return;
    dx /= l;
    dy /= l;
    float hx = x1 - dx * 9, hy = y1 - dy * 9;
    gui_line(x1, y1, hx - dy * 5, hy + dx * 5, c);
    gui_line(x1, y1, hx + dy * 5, hy - dx * 5, c);
}

static void thick_frame(float x0, float y0, float x1, float y1, unsigned int c, int t)
{
    if (x0 > x1) { float k = x0; x0 = x1; x1 = k; }
    if (y0 > y1) { float k = y0; y0 = y1; y1 = k; }
    for (int i = 0; i < t; i++)
        gui_frame((int)x0 - i, (int)y0 - i, (int)(x1 - x0) + 1 + i * 2, (int)(y1 - y0) + 1 + i * 2, c);
}

static void tag(float x, float y, int number, unsigned int c, int done)
{
    if (y < 0) y = 0;
    if (x < 0) x = 0;
    char t[8];
    snprintf(t, sizeof(t), "%d", number);
    int w = font_width(t) + 6;
    gui_rect((int)x, (int)y, w, 15, done ? 0x2A2F32 : c);
    font_draw((int)x + 3, (int)y - 1, done ? C_DIM : 0x17181B, t);
}

static void draw_shape(const View *v, int is2d, const Json *n, const float *vs, unsigned int c, int thick, int number, int done)
{
    const Json *sh = json_get(n, "shape");
    float r[4], sx, sy, ex, ey, tx = 0, ty = 0;
    if (json_getv(sh, "rect", r, 4) == 4) {
        to_screen(v, is2d, vs, r[0], r[1], &sx, &sy);
        to_screen(v, is2d, vs, r[0] + r[2], r[1] + r[3], &ex, &ey);
        thick_frame(sx, sy, ex, ey, c, thick);
        tx = fminf(sx, ex); ty = fminf(sy, ey) - 16;
    } else if (json_getv(sh, "circle", r, 3) == 3) {
        to_screen(v, is2d, vs, r[0], r[1], &sx, &sy);
        to_screen(v, is2d, vs, r[0] + r[2], r[1], &ex, &ey);
        float rad = fabsf(ex - sx);
        for (int i = 0; i < thick; i++) gui_circle(sx, sy, rad + i, c);
        tx = sx - rad; ty = sy - rad - 16;
    } else if (json_getv(sh, "arrow", r, 4) == 4) {
        to_screen(v, is2d, vs, r[0], r[1], &sx, &sy);
        to_screen(v, is2d, vs, r[2], r[3], &ex, &ey);
        arrow(sx, sy, ex, ey, c);
        tx = sx - 8; ty = sy - 16;
    } else if (json_getv(sh, "pin", r, 2) == 2) {
        to_screen(v, is2d, vs, r[0], r[1], &sx, &sy);
        gui_disc(sx, sy - 9, 5, c);
        gui_line(sx, sy - 5, sx, sy, c);
        if (thick > 1) gui_circle(sx, sy - 9, 7, c);
        tx = sx + 7; ty = sy - 22;
    } else {
        const Json *pts = json_get(sh, "points");
        float px = 0, py = 0;
        int first = 1;
        for (const Json *p = pts ? pts->child : NULL; p; p = p->next) {
            float q[2] = {0, 0};
            int k = 0;
            for (const Json *e = p->child; e && k < 2; e = e->next) q[k++] = (float)e->num;
            to_screen(v, is2d, vs, q[0], q[1], &sx, &sy);
            if (!first) { gui_line(px, py, sx, sy, c); if (thick > 1) gui_line(px + 1, py, sx + 1, sy, c); }
            else { tx = sx; ty = sy - 16; }
            first = 0;
            px = sx;
            py = sy;
        }
    }
    if (number > 0) tag(tx, ty, number, c, done);
}

static int same_view(const Cam *a, const Cam *b)
{
    return a->ortho == b->ortho && fabsf(a->yaw - b->yaw) < 0.6f && fabsf(a->pitch - b->pitch) < 0.6f &&
           fabsf(a->dist - b->dist) < a->dist * 0.02f + 0.01f && fabsf(a->target[0] - b->target[0]) < 0.02f &&
           fabsf(a->target[1] - b->target[1]) < 0.02f && fabsf(a->target[2] - b->target[2]) < 0.02f;
}

void notes_draw(const View *v, Json *scene, int for_snapshot, const char *highlight)
{
    Json *notes = json_get(scene, "notes");
    int is2d = scene_is_2d(scene);
    int k = 0;
    for (Json *n = notes ? notes->child : NULL; n; n = n->next) {
        k++;
        int done = !strcmp(json_gets(n, "status", "open"), "done");
        if (done && !for_snapshot && !(highlight && !strcmp(highlight, json_gets(n, "id", "")))) {
            // Resolved notes fade to a small grey tag.
        }
        unsigned int c = parse_color(json_gets(n, "color", NULL), C_NOTE);
        if (done) c = 0x5A6064;
        int hl = highlight && highlight[0] && !strcmp(highlight, json_gets(n, "id", ""));
        if (for_snapshot && highlight && highlight[0] && !hl) c = 0x6A7277;
        float vs[2] = {0, 0};
        json_getv(n, "view_size", vs, 2);
        if (!is2d) {
            Cam nc;
            cam_from_json(&nc, json_get(n, "camera"));
            if (!same_view(&nc, &v->cam)) {
                // Another view: only a numbered pin at the world point.
                float w[3], sx, sy;
                if (json_getv(n, "world", w, 3) == 3 && world_to_screen(v, w, &sx, &sy)) {
                    gui_disc(sx, sy, hl ? 5 : 4, c);
                    gui_circle(sx, sy, hl ? 7 : 5, 0x17181B);
                    tag(sx + 6, sy - 16, k, c, done);
                }
                continue;
            }
        }
        draw_shape(v, is2d, n, is2d ? NULL : vs, c, hl ? 2 : 1, k, done);
        if (hl && !for_snapshot) {
            // Text bubble next to the selected note.
            const char *t = json_gets(n, "text", "");
            if (t[0]) {
                float r[4] = {0, 0, 0, 0}, sx, sy;
                const Json *sh = json_get(n, "shape");
                if (!json_getv(sh, "rect", r, 4) && !json_getv(sh, "circle", r, 3) && !json_getv(sh, "arrow", r, 2) && !json_getv(sh, "pin", r, 2)) r[0] = r[1] = 10;
                to_screen(v, is2d, is2d ? NULL : vs, r[0], r[1], &sx, &sy);
                int w = font_width(t) + 12;
                if (w > 260) w = 260;
                int lines = gui_text_wrap(0, 0, w - 12, 0, t, 0);
                int bx = (int)sx, by = (int)sy - 22 - lines * font_line_h();
                if (by < v->y + 2) by = (int)sy + 4;
                gui_rect_a(bx, by, w, lines * font_line_h() + 6, 0x17181B, 230);
                gui_frame(bx, by, w, lines * font_line_h() + 6, c);
                gui_text_wrap(bx + 6, by + 2, w - 12, C_BRIGHT, t, 1);
            }
        }
    }
}

// ---------------------------------------------------------------- tools
static void draft_preview(const View *v, int is2d)
{
    float sx, sy, ex, ey;
    to_screen(v, is2d, NULL, s_draft.a[0], s_draft.a[1], &sx, &sy);
    to_screen(v, is2d, NULL, s_draft.b[0], s_draft.b[1], &ex, &ey);
    if (!is2d) { sx = v->x + s_draft.a[0]; sy = v->y + s_draft.a[1]; ex = v->x + s_draft.b[0]; ey = v->y + s_draft.b[1]; }
    switch (s_draft.kind) {
    case TOOL_BOX: thick_frame(sx, sy, ex, ey, C_NOTE, 1); break;
    case TOOL_CIRCLE: gui_circle(sx, sy, sqrtf((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy)), C_NOTE); break;
    case TOOL_ARROW: arrow(sx, sy, ex, ey, C_NOTE); break;
    case TOOL_PIN: gui_disc(ex, ey - 9, 5, C_NOTE); gui_line(ex, ey - 5, ex, ey, C_NOTE); break;
    default:
        for (int i = 1; i < s_draft.npts; i++) {
            float ax, ay, bx, by;
            to_screen(v, is2d, NULL, s_draft.pts[i - 1][0], s_draft.pts[i - 1][1], &ax, &ay);
            to_screen(v, is2d, NULL, s_draft.pts[i][0], s_draft.pts[i][1], &bx, &by);
            if (!is2d) { ax = v->x + s_draft.pts[i - 1][0]; ay = v->y + s_draft.pts[i - 1][1]; bx = v->x + s_draft.pts[i][0]; by = v->y + s_draft.pts[i][1]; }
            gui_line(ax, ay, bx, by, C_NOTE);
        }
        break;
    }
}

void notes_tool_input(const View *v, int vx, int vy, int vw, int vh)
{
    Json *scene = cur_scene();
    if (!scene) return;
    int is2d = scene_is_2d(scene);
    int note_tool = ed.tool >= TOOL_BOX;
    float x, y;
    if (s_typing) { draft_preview(v, is2d); return; }  // the popup owns the input
    if (!note_tool) return;
    int over = gui_hit(vx, vy, vw, vh) && !gui.mouse_used && !(gui.active && !s_drawing);
    if (over) input_set_cursor(4);
    if (over && gui.clicked && !(input_mods() & MOD_ALT)) {
        from_screen(v, is2d, (float)gui.mx, (float)gui.my, &x, &y);
        memset(&s_draft, 0, sizeof(s_draft));
        s_draft.kind = ed.tool;
        s_draft.a[0] = s_draft.b[0] = x;
        s_draft.a[1] = s_draft.b[1] = y;
        s_draft.pts[0][0] = x;
        s_draft.pts[0][1] = y;
        s_draft.npts = 1;
        s_drawing = 1;
        s_scene_index = ed.cur;
        s_cam = ed.cams[ed.cur];
        s_vw = v->w;
        s_vh = v->h;
        gui_take_mouse();
    }
    if (!s_drawing) return;
    from_screen(v, is2d, (float)gui.mx, (float)gui.my, &x, &y);
    s_draft.b[0] = x;
    s_draft.b[1] = y;
    if (s_draft.kind == TOOL_PEN && s_draft.npts < 256) {
        float *l = s_draft.pts[s_draft.npts - 1];
        if (fabsf(l[0] - x) + fabsf(l[1] - y) >= 3) { s_draft.pts[s_draft.npts][0] = x; s_draft.pts[s_draft.npts][1] = y; s_draft.npts++; }
    }
    gui_take_mouse();
    draft_preview(v, is2d);
    if (!(gui.in.mouse_buttons & 1)) {
        s_drawing = 0;
        float dx = fabsf(s_draft.b[0] - s_draft.a[0]), dy = fabsf(s_draft.b[1] - s_draft.a[1]);
        if (s_draft.kind != TOOL_PIN && dx < 3 && dy < 3 && s_draft.npts < 3) {
            // A click with a shape tool: treat as a pin at that point.
            s_draft.kind = TOOL_PIN;
        }
        s_typing = 1;
        s_text[0] = 0;
        gui_focus(gui_id("note-text", 0), "");
    }
}

static Json *draft_shape(void)
{
    Json *sh = json_obj();
    float a0 = s_draft.a[0], a1 = s_draft.a[1], b0 = s_draft.b[0], b1 = s_draft.b[1];
    switch (s_draft.kind) {
    case TOOL_BOX: {
        float r[4] = {fminf(a0, b0), fminf(a1, b1), fabsf(b0 - a0), fabsf(b1 - a1)};
        json_setv(sh, "rect", r, 4);
        break;
    }
    case TOOL_CIRCLE: {
        float r[3] = {a0, a1, roundf(sqrtf((b0 - a0) * (b0 - a0) + (b1 - a1) * (b1 - a1)))};
        json_setv(sh, "circle", r, 3);
        break;
    }
    case TOOL_ARROW: {
        // The arrow points at what matters: its head (b) is the target.
        float r[4] = {a0, a1, b0, b1};
        json_setv(sh, "arrow", r, 4);
        break;
    }
    case TOOL_PIN: {
        float r[2] = {b0, b1};
        json_setv(sh, "pin", r, 2);
        break;
    }
    default: {
        Json *pts = json_set(sh, "points", json_arr());
        for (int i = 0; i < s_draft.npts; i++) {
            Json *p = json_arr();
            json_push(p, json_num(s_draft.pts[i][0]));
            json_push(p, json_num(s_draft.pts[i][1]));
            json_push(pts, p);
        }
        break;
    }
    }
    return sh;
}

void notes_popup(void)
{
    if (!s_typing) return;
    Json *scene = scene_at(s_scene_index);
    if (!scene) { s_typing = 0; return; }
    int w = 300, h = 74;
    int x = ed.view.x + 20, y = ed.view.y + ed.view.h - h - 26;
    gui_rect_a(x + 3, y + 3, w, h, 0x000000, 150);
    gui_panel(x, y, w, h);
    font_draw(x + 8, y + 5, C_BRIGHT, T("note.new"));
    GuiId fid = gui_id("note-text", 0);
    if (!gui_focused(fid) && !gui.edit_commit) gui_focus(fid, s_text);
    int commit = gui_field(fid, x + 8, y + 24, w - 16, s_text, sizeof(s_text));
    int save = gui_button("button.save", x + w - 156, y + 48, 72, 18);
    int cancel = gui_button("button.cancel", x + w - 80, y + 48, 72, 18);
    if (input_key_pressed(KEY_ESCAPE)) cancel = 1;
    if (commit && gui.edit_commit && !cancel) save = 1;
    if (!save && !cancel) return;
    if (save) {
        Json *a = json_obj();
        json_sets(a, "scene", scene_id(scene));
        json_sets(a, "kind", kind_name(s_draft.kind));
        json_sets(a, "text", s_text);
        json_set(a, "shape", draft_shape());
        if (!scene_is_2d(scene)) {
            json_set(a, "camera", cam_to_json(&s_cam));
            json_setn(a, "view_w", s_vw);
            json_setn(a, "view_h", s_vh);
        }
        if (ops_do("note.add", a)) ed_status(C_OK, "%s", T("status.note_saved"));
    }
    s_typing = 0;
    gui.focus = 0;
}

// ---------------------------------------------------------------- snapshots
typedef struct {
    char scene[64];
    Cam cam;
    int w, h;
    char highlight[64];
    int has_highlight;
    char path[ED_PATH];
} SnapJob;

static SnapJob s_jobs[16];
static int s_njobs;

static int render_now(Json *scene, const Cam *cam, int w, int h, const char *highlight, const char *path)
{
    int is2d = scene_is_2d(scene);
    View v;
    memset(&v, 0, sizeof(v));
    if (is2d) {
        float c[2] = {1280, 720};
        json_getv(json_get(scene, "settings"), "canvas", c, 2);
        w = (int)c[0];
        h = (int)c[1];

        v.x = 0; v.y = 0; v.w = w; v.h = h; v.cx = 0; v.cy = 0; v.cscale = 1;
        v.canvas_w = w; v.canvas_h = h;
    } else view3d_setup(&v, cam, 0, 0, w, h);
    // Snapshots are drawn at one pixel per point, so image pixels are shape coordinates.
    int keep_scale = r_ui_scale(), keep_w = 0, keep_h = 0;
    r_size(&keep_w, &keep_h);
    if (w > keep_w || h > keep_h) {
        int nw = w > keep_w ? w : keep_w, nh = h > keep_h ? h : keep_h;
        if (r_set_frame_size(nw, nh) != 0) return -1;
    }
    r_set_ui_scale(1);
    view3d_new_frame();
    gfx_begin_3d();
    gfx_begin_2d();
    gui_unclip();
    if (is2d) {
        const char *bg = json_gets(json_get(scene, "settings"), "background", "");
        gui_rect(0, 0, w, h, parse_color(bg, 0x1B1C1F));
        draw_scene_2d(&v, scene, 0);
    } else {
        Json *set = json_get(scene, "settings");
        gui_rect(0, 0, w, h, json_getb(set, "fog_enabled", 0) ? parse_color(json_gets(set, "fog_color", NULL), 0x34383C)
                                                              : parse_color(json_gets(set, "background", NULL), 0x3A3D42));
        draw_scene_3d(&v, scene, 0);
    }
    if (highlight) notes_draw(&v, scene, 1, highlight);
    gfx_finish();
    int fw = 0, fh = 0;
    r_size(&fw, &fh);
    unsigned char *fb = (unsigned char *)malloc((size_t)fw * (size_t)fh * 3);
    r_set_ui_scale(keep_scale);
    if (!fb) return -1;
    r_copy_frame_rgb(fb);
    // Crop the w x h corner the snapshot was drawn in.
    for (int y = 1; y < h; y++) memmove(fb + (size_t)y * w * 3, fb + (size_t)y * fw * 3, (size_t)w * 3);
    char dir[ED_PATH];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; plat_mkdirs(dir); }
    int r = png_save_rgb(path, fb, w, h);
    if (fw != keep_w || fh != keep_h) r_set_frame_size(keep_w, keep_h);
    free(fb);
    if (r != 0) ed_log(LOG_ERR, "snapshot: cannot write %s", path);
    return r;
}

int render_snapshot(Json *scene, const Cam *cam, int w, int h, const char *highlight, const char *path)
{
    if (!ed_in_frame) return render_now(scene, cam, w, h, highlight, path);
    if (s_njobs >= 16) return -1;
    SnapJob *j = &s_jobs[s_njobs++];
    snprintf(j->scene, sizeof(j->scene), "%s", scene_id(scene));
    j->cam = *cam;
    j->w = w;
    j->h = h;
    j->has_highlight = highlight != NULL;
    snprintf(j->highlight, sizeof(j->highlight), "%s", highlight ? highlight : "");
    snprintf(j->path, sizeof(j->path), "%s", path);
    return 0;
}

// Called between frames by the main loop.
void snapshots_process(void)
{
    for (int i = 0; i < s_njobs; i++) {
        SnapJob *j = &s_jobs[i];
        Json *s = scene_by_id(j->scene);
        if (s) render_now(s, &j->cam, j->w, j->h, j->has_highlight ? j->highlight : NULL, j->path);
    }
    s_njobs = 0;
}
