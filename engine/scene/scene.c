#include "scene.h"
#include "engine/render/render_api.h"
#include "engine/render/model.h"
#include "engine/render/font.h"
#include "engine/render/ui.h"
#include "engine/render/gfx.h"
#include "engine/render/primitives.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEG 0.017453293f
#define MAX_SCENES 64

struct MxProject {
    Json *project;
    Json *scenes[MAX_SCENES];
    int visible[MAX_SCENES];
    int n;
    MxPortFn on_port;
    void *user;
};

// ---------------------------------------------------------------- loading
MxProject *mx_project_load(const char *dir)
{
    char path[1024], err[256];
    snprintf(path, sizeof(path), "%s/project.json", dir);
    Json *pj = json_load(path, err, sizeof(err));
    if (!pj) { fprintf(stderr, "mx_project_load: %s\n", err); return NULL; }
    MxProject *p = (MxProject *)calloc(1, sizeof(MxProject));
    if (!p) { json_free(pj); return NULL; }
    p->project = pj;
    Json *order = json_get(pj, "scenes");
    for (Json *c = order ? order->child : NULL; c && p->n < MAX_SCENES; c = c->next) {
        if (c->type != JSON_STR) continue;
        snprintf(path, sizeof(path), "%s/scenes/%s.json", dir, c->str);
        Json *s = json_load(path, err, sizeof(err));
        if (!s) { fprintf(stderr, "mx_project_load: %s\n", err); continue; }
        p->visible[p->n] = json_getb(s, "visible", 1);
        p->scenes[p->n++] = s;
    }
    return p;
}

void mx_project_free(MxProject *p)
{
    if (!p) return;
    for (int i = 0; i < p->n; i++) json_free(p->scenes[i]);
    json_free(p->project);
    free(p);
}

int mx_scene_count(const MxProject *p) { return p ? p->n : 0; }
const char *mx_scene_id(const MxProject *p, int i) { return p && i >= 0 && i < p->n ? json_gets(p->scenes[i], "id", "") : ""; }
int mx_scene_is_2d(const MxProject *p, int i) { return !strcmp(json_gets(p->scenes[i], "kind", "3d"), "2d"); }
Json *mx_scene_doc(MxProject *p, int i) { return p && i >= 0 && i < p->n ? p->scenes[i] : NULL; }
int mx_scene_visible(const MxProject *p, int i) { return p && i >= 0 && i < p->n ? p->visible[i] : 0; }
void mx_scene_set_visible(MxProject *p, int i, int on) { if (p && i >= 0 && i < p->n) p->visible[i] = on ? 1 : 0; }

int mx_scene_find(const MxProject *p, const char *id)
{
    for (int i = 0; p && id && i < p->n; i++)
        if (!strcmp(json_gets(p->scenes[i], "id", ""), id)) return i;
    return -1;
}

Json *mx_object(MxProject *p, int scene, const char *id)
{
    Json *s = mx_scene_doc(p, scene);
    return s ? json_find_id(json_get(s, "objects"), id) : NULL;
}

// ---------------------------------------------------------------- camera
void mx_scene_camera(const MxProject *p, int scene, MxCamera *c)
{
    memset(c, 0, sizeof(*c));
    c->target[1] = 0.5f; c->yaw = 35; c->pitch = 28; c->dist = 12; c->fov = 55;
    Json *j = p && scene >= 0 && scene < p->n ? json_get(p->scenes[scene], "camera") : NULL;
    if (!j) return;
    json_getv(j, "target", c->target, 3);
    c->yaw = (float)json_getn(j, "yaw", c->yaw);
    c->pitch = (float)json_getn(j, "pitch", c->pitch);
    c->dist = (float)json_getn(j, "dist", c->dist);
    c->fov = (float)json_getn(j, "fov", c->fov);
    c->ortho = json_getb(j, "ortho", 0);
}

void mx_camera_eye(const MxCamera *c, float eye[3])
{
    float p = c->pitch * DEG, y = c->yaw * DEG, d = c->ortho ? 400.0f : c->dist;
    eye[0] = c->target[0] + cosf(p) * sinf(y) * d;
    eye[1] = c->target[1] + sinf(p) * d;
    eye[2] = c->target[2] + cosf(p) * cosf(y) * d;
}

void mx_camera_apply(const MxCamera *c, int w, int h)
{
    float aspect = h > 0 ? (float)w / (float)h : 1.0f;
    r_matrix_mode(R_MATRIX_PROJECTION);
    r_load_identity();
    if (c->ortho) {
        RMat4 m;
        memset(&m, 0, sizeof(m));
        float oh = c->dist * tanf(c->fov * 0.5f * DEG), n = 1.0f, f = 1000.0f;
        m.m[0] = 1.0f / (oh * aspect);
        m.m[5] = 1.0f / oh;
        m.m[10] = -2.0f / (f - n);
        m.m[14] = -(f + n) / (f - n);
        m.m[15] = 1.0f;
        r_load_matrix(&m);
        r_set_near(1.0f);
    } else {
        r_perspective(c->fov, aspect, 0.1f, 600.0f);
        r_set_near(0.1f);
    }
    float eye[3];
    mx_camera_eye(c, eye);
    r_matrix_mode(R_MATRIX_VIEW);
    r_load_identity();
    RVec3 e = {eye[0], eye[1], eye[2]}, t = {c->target[0], c->target[1], c->target[2]}, up = {0, 1, 0};
    r_look_at(&e, &t, &up);
    r_matrix_mode(R_MATRIX_MODEL);
    r_load_identity();
}

static unsigned int parse_rgb(const char *s, unsigned int def)
{
    if (!s) return def;
    if (*s == '#') s++;
    char *end;
    unsigned long v = strtoul(s, &end, 16);
    return end - s == 6 ? (unsigned int)v : def;
}

typedef struct { char path[128]; Model m; int ok; } ModelSlot;
static ModelSlot s_models[64];

static const Model *model_get(const char *path)
{
    if (!path || !*path) return NULL;
    for (int i = 0; i < 64; i++)
        if (s_models[i].path[0] && !strcmp(s_models[i].path, path)) return s_models[i].ok ? &s_models[i].m : NULL;
    for (int i = 0; i < 64; i++)
        if (!s_models[i].path[0]) {
            snprintf(s_models[i].path, sizeof(s_models[i].path), "%s", path);
            s_models[i].ok = model_load(&s_models[i].m, path) == 0;
            if (!s_models[i].ok) fprintf(stderr, "scene: model %s not found\n", path);
            return s_models[i].ok ? &s_models[i].m : NULL;
        }
    return NULL;
}

static void draw_object(const Json *o)
{
    const char *t = json_gets(o, "type", "box");
    // Editor-only helpers have no look in the game.
    if (!strcmp(t, "light") || !strcmp(t, "marker") || !strcmp(t, "camera")) return;
    float pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, sc[3] = {1, 1, 1};
    json_getv(o, "pos", pos, 3);
    json_getv(o, "rot", rot, 3);
    json_getv(o, "scale", sc, 3);
    if (!strcmp(t, "model")) {
        const Model *m = model_get(json_gets(o, "model", NULL));
        if (!m) return;
        r_matrix_mode(R_MATRIX_MODEL);
        r_load_identity();
        RVec3 p = {pos[0], pos[1], pos[2]}, s = {sc[0], sc[1], sc[2]};
        r_translate(&p);
        r_rotate_y(rot[1] * DEG);
        r_rotate_x(rot[0] * DEG);
        r_rotate_z(rot[2] * DEG);
        r_scale(&s);
        r_set_cull(1);
        model_draw(m);
        r_set_cull(0);
        r_load_identity();
        return;
    }
    RShape shape = !strcmp(t, "plane") ? R_SHAPE_PLANE : !strcmp(t, "sphere") ? R_SHAPE_SPHERE
                  : !strcmp(t, "cylinder") ? R_SHAPE_CYLINDER : !strcmp(t, "cone") ? R_SHAPE_CONE
                  : R_SHAPE_BOX;
    unsigned int rgb = parse_rgb(json_gets(o, "color", NULL), 0xB4B8BE);
    uint32_t abgr = 0xFF000000u | ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);
    MxQuat rot_q = mx_quat_from_euler(mx_v3(rot[0], rot[1], rot[2]));
    r_draw_shape(shape, mx_v3(pos[0], pos[1], pos[2]), rot_q, mx_v3(sc[0], sc[1], sc[2]), abgr);
}

void mx_scene_draw_3d(MxProject *p, int scene)
{
    Json *s = mx_scene_doc(p, scene);
    if (!s) return;
    r_set_cull(0);
    Json *set = json_get(s, "settings");
    if (json_getb(set, "fog_enabled", 0)) {
        float fog[2] = {20, 80};
        json_getv(set, "fog", fog, 2);
        unsigned int c = parse_rgb(json_gets(set, "fog_color", NULL), 0x808890);
        r_set_fog(1);
        r_set_fog_params(fog[0], fog[1], 0xFF000000u | (c & 0xFF) << 16 | (c & 0xFF00) | ((c >> 16) & 0xFF));
    } else r_set_fog(0);
    Json *objs = json_get(s, "objects");
    for (Json *o = objs ? objs->child : NULL; o; o = o->next)
        if (json_getb(o, "visible", 1)) draw_object(o);
}

void mx_scene_draw_2d(MxProject *p, int scene, int x, int y, float scale)
{
    Json *s = mx_scene_doc(p, scene);
    if (!s) return;
    int ts = (int)scale;
    if (ts < 1) ts = 1;
    Json *objs = json_get(s, "objects");
    for (Json *o = objs ? objs->child : NULL; o; o = o->next) {
        if (!json_getb(o, "visible", 1)) continue;
        float r[4] = {0, 0, 40, 16};
        json_getv(o, "rect", r, 4);
        int ox = x + (int)(r[0] * scale), oy = y + (int)(r[1] * scale), w = (int)(r[2] * scale), h = (int)(r[3] * scale);
        const char *t = json_gets(o, "type", "rect");
        unsigned int col = parse_rgb(json_gets(o, "color", NULL), 0x3A3D42);
        const char *text = json_gets(o, "text", "");
        if (!strcmp(t, "text")) { font_draw_scaled(ox, oy, col, text, -1, ts); continue; }
        if (!strcmp(t, "image")) continue;  // images are the game's to draw (any format it likes)
        ui_rect(ox, oy, w, h, col, 255);
        if (!strcmp(t, "panel") || !strcmp(t, "button")) ui_frame(ox, oy, w, h, 0x7A7F87);
        if (!strcmp(t, "button")) {
            int tw = font_width(text) * ts;
            font_draw_scaled(ox + (w - tw) / 2, oy + (h - font_cell_h() * ts) / 2, 0xE6E6E6, text, -1, ts);
        }
    }
}

void mx_project_draw_2d(MxProject *p, int w, int h)
{
    gfx_begin_2d();
    for (int i = 0; i < mx_scene_count(p); i++) {
        if (!p->visible[i] || !mx_scene_is_2d(p, i)) continue;
        float c[2] = {1280, 720};
        json_getv(json_get(p->scenes[i], "settings"), "canvas", c, 2);
        float k = fminf(w / c[0], h / c[1]);
        mx_scene_draw_2d(p, i, (int)((w - c[0] * k) / 2), (int)((h - c[1] * k) / 2), k);
    }
}

void mx_project_draw(MxProject *p, const MxCamera *cam, int w, int h)
{
    for (int i = 0; i < mx_scene_count(p); i++)
        if (p->visible[i] && !mx_scene_is_2d(p, i)) {
            mx_camera_apply(cam, w, h);
            mx_scene_draw_3d(p, i);
        }
    mx_project_draw_2d(p, w, h);
}

// ---------------------------------------------------------------- lines and tunnels
void mx_on_port(MxProject *p, MxPortFn fn, void *user) { if (p) { p->on_port = fn; p->user = user; } }

static void split(const char *ref, char *node, int nsize, const char **port)
{
    const char *dot = strchr(ref, '.');
    int n = dot ? (int)(dot - ref) : (int)strlen(ref);
    if (n >= nsize) n = nsize - 1;
    memcpy(node, ref, (size_t)n);
    node[n] = 0;
    *port = dot ? dot + 1 : "";
}

static void reached(MxProject *p, const char *ref)
{
    char node[64];
    const char *port;
    split(ref, node, sizeof(node), &port);
    int s = mx_scene_find(p, node);
    if (s >= 0 && !strcmp(port, "show")) p->visible[s] = 1;
    if (s >= 0 && !strcmp(port, "hide")) p->visible[s] = 0;
    if (p->on_port) p->on_port(p, node, port, p->user);
}

int mx_emit(MxProject *p, const char *from)
{
    if (!p || !from) return 0;
    char queue[64][140], seen[64][140];
    int qh = 0, qt = 0, ns = 0;
    snprintf(queue[qt++], 140, "%s", from);
    Json *lines = json_get(p->project, "lines"), *tunnels = json_get(p->project, "tunnels");
    while (qh < qt) {
        const char *cur = queue[qh++];
        for (Json *l = lines ? lines->child : NULL; l; l = l->next) {
            if (strcmp(json_gets(l, "from", ""), cur)) continue;
            const char *to = json_gets(l, "to", "");
            int dup = 0;
            for (int i = 0; i < ns; i++) if (!strcmp(seen[i], to)) dup = 1;
            if (dup || ns >= 64) continue;
            snprintf(seen[ns++], 140, "%s", to);
            reached(p, to);
            char node[64];
            const char *port;
            split(to, node, sizeof(node), &port);
            if (json_find_id(tunnels, node) && qt < 64) snprintf(queue[qt++], 140, "%s", to);
        }
    }
    return ns;
}
