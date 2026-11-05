#include "scene.h"
#include "engine/render/render_api.h"
#include "engine/render/model.h"
#include "engine/render/font.h"
#include "engine/render/ui.h"
#include "engine/render/gfx.h"

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

// ---------------------------------------------------------------- primitives
typedef struct { float p[3], n[3]; } PV;
#define PRIM_MAX 600
typedef struct { PV v[PRIM_MAX]; int n; } Mesh;
enum { P_BOX, P_PLANE, P_SPHERE, P_CYL, P_CONE, P_COUNT };
static Mesh s_mesh[P_COUNT];
static RVertex s_buf[PRIM_MAX];

static void v3(float *o, float x, float y, float z) { o[0] = x; o[1] = y; o[2] = z; }

static void tri(Mesh *m, const float *a, const float *b, const float *c)
{
    if (m->n + 3 > PRIM_MAX) return;
    float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l > 0) { n[0] /= l; n[1] /= l; n[2] /= l; }
    const float *pts[3] = {a, b, c};
    for (int i = 0; i < 3; i++, m->n++) {
        memcpy(m->v[m->n].p, pts[i], 12);
        memcpy(m->v[m->n].n, n, 12);
    }
}

static void quad(Mesh *m, const float *a, const float *b, const float *c, const float *d) { tri(m, a, b, c); tri(m, a, c, d); }

static void build(void)
{
    if (s_mesh[P_BOX].n) return;
    float c[8][3];
    for (int i = 0; i < 8; i++) v3(c[i], (i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
    Mesh *b = &s_mesh[P_BOX];
    quad(b, c[0], c[2], c[3], c[1]); quad(b, c[4], c[5], c[7], c[6]); quad(b, c[0], c[4], c[6], c[2]);
    quad(b, c[1], c[3], c[7], c[5]); quad(b, c[2], c[6], c[7], c[3]); quad(b, c[0], c[1], c[5], c[4]);
    float p0[3] = {-0.5f, 0, -0.5f}, p1[3] = {0.5f, 0, -0.5f}, p2[3] = {0.5f, 0, 0.5f}, p3[3] = {-0.5f, 0, 0.5f};
    quad(&s_mesh[P_PLANE], p0, p3, p2, p1);
    const int SEG = 10, RING = 6;
    for (int r = 0; r < RING; r++)
        for (int s = 0; s < SEG; s++) {
            float a0 = 6.2831853f * s / SEG, a1 = 6.2831853f * (s + 1) / SEG;
            float t0 = 3.14159265f * r / RING - 1.5707963f, t1 = 3.14159265f * (r + 1) / RING - 1.5707963f;
            float q[4][3];
            v3(q[0], cosf(t0) * cosf(a0) * 0.5f, sinf(t0) * 0.5f, cosf(t0) * sinf(a0) * 0.5f);
            v3(q[1], cosf(t0) * cosf(a1) * 0.5f, sinf(t0) * 0.5f, cosf(t0) * sinf(a1) * 0.5f);
            v3(q[2], cosf(t1) * cosf(a1) * 0.5f, sinf(t1) * 0.5f, cosf(t1) * sinf(a1) * 0.5f);
            v3(q[3], cosf(t1) * cosf(a0) * 0.5f, sinf(t1) * 0.5f, cosf(t1) * sinf(a0) * 0.5f);
            quad(&s_mesh[P_SPHERE], q[0], q[3], q[2], q[1]);
        }
    for (int s = 0; s < SEG; s++) {
        float a0 = 6.2831853f * s / SEG, a1 = 6.2831853f * (s + 1) / SEG;
        float b0[3] = {cosf(a0) * 0.5f, -0.5f, sinf(a0) * 0.5f}, b1[3] = {cosf(a1) * 0.5f, -0.5f, sinf(a1) * 0.5f};
        float t0[3] = {b0[0], 0.5f, b0[2]}, t1[3] = {b1[0], 0.5f, b1[2]}, top[3] = {0, 0.5f, 0}, bot[3] = {0, -0.5f, 0};
        quad(&s_mesh[P_CYL], b0, t0, t1, b1);
        tri(&s_mesh[P_CYL], top, t1, t0);
        tri(&s_mesh[P_CYL], bot, b0, b1);
        tri(&s_mesh[P_CONE], b0, top, b1);
        tri(&s_mesh[P_CONE], bot, b0, b1);
    }
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
    int k = !strcmp(t, "plane") ? P_PLANE : !strcmp(t, "sphere") ? P_SPHERE : !strcmp(t, "cylinder") ? P_CYL
          : !strcmp(t, "cone") ? P_CONE : P_BOX;
    const Mesh *m = &s_mesh[k];
    unsigned int rgb = parse_rgb(json_gets(o, "color", NULL), 0xB4B8BE);
    // R = Ry * Rx * Rz, the editor's convention.
    float cx = cosf(rot[0] * DEG), sx = sinf(rot[0] * DEG), cy = cosf(rot[1] * DEG), sy = sinf(rot[1] * DEG);
    float cz = cosf(rot[2] * DEG), sz = sinf(rot[2] * DEG);
    float R[9] = {cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx,
                  cx * sz, cx * cz, -sx,
                  -sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx};
    static const float L[3] = {0.42f, 0.81f, 0.40f};
    for (int i = 0; i < m->n; i++) {
        float lp[3] = {m->v[i].p[0] * sc[0], m->v[i].p[1] * sc[1], m->v[i].p[2] * sc[2]};
        float ln[3] = {m->v[i].n[0] / sc[0], m->v[i].n[1] / sc[1], m->v[i].n[2] / sc[2]}, wn[3];
        float wp[3];
        for (int r = 0; r < 3; r++) {
            wp[r] = pos[r] + R[r * 3] * lp[0] + R[r * 3 + 1] * lp[1] + R[r * 3 + 2] * lp[2];
            wn[r] = R[r * 3] * ln[0] + R[r * 3 + 1] * ln[1] + R[r * 3 + 2] * ln[2];
        }
        float l = sqrtf(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
        float d = l > 0 ? (wn[0] * L[0] + wn[1] * L[1] + wn[2] * L[2]) / l : 0;
        float shade = 0.42f + 0.58f * (d > 0 ? d : 0);
        int rr = (int)(((rgb >> 16) & 0xFF) * shade), gg = (int)(((rgb >> 8) & 0xFF) * shade), bb = (int)((rgb & 0xFF) * shade);
        s_buf[i] = (RVertex){wp[0], wp[1], wp[2], 0, 0, 0xFF000000u | (unsigned)bb << 16 | (unsigned)gg << 8 | (unsigned)rr};
    }
    r_bind_texture(NULL);
    r_draw(R_PRIM_TRIANGLES, s_buf, m->n);
}

void mx_scene_draw_3d(MxProject *p, int scene)
{
    Json *s = mx_scene_doc(p, scene);
    if (!s) return;
    build();
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

void mx_project_draw(MxProject *p, const MxCamera *cam, int w, int h)
{
    // 3D layers first (the caller is in the 3D pass), then 2D layers over them.
    for (int i = 0; i < mx_scene_count(p); i++)
        if (p->visible[i] && !mx_scene_is_2d(p, i)) {
            mx_camera_apply(cam, w, h);
            mx_scene_draw_3d(p, i);
        }
    gfx_begin_2d();
    for (int i = 0; i < mx_scene_count(p); i++) {
        if (!p->visible[i] || !mx_scene_is_2d(p, i)) continue;
        float c[2] = {1280, 720};
        json_getv(json_get(p->scenes[i], "settings"), "canvas", c, 2);
        float k = fminf(w / c[0], h / c[1]);
        mx_scene_draw_2d(p, i, (int)((w - c[0] * k) / 2), (int)((h - c[1] * k) / 2), k);
    }
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
