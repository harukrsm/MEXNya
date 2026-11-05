// The scene view: a Unity-style 3D viewport (orbit, pan, fly, preset views, picking,
// move/rotate/scale gizmos) and the 2D canvas for UI scenes. Scenes are layers: every
// visible scene is drawn bottom to top; only the active one is edited.
#include "ed.h"
#include "gui.h"
#include "engine/render/render_api.h"
#include "engine/render/gfx.h"
#include "engine/render/model.h"
#include "engine/render/font.h"
#include "engine/render/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEG 0.017453293f

// ---------------------------------------------------------------- vector helpers
static void v3(float *o, float x, float y, float z) { o[0] = x; o[1] = y; o[2] = z; }
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(float *o, const float *a, const float *b)
{
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    v3(o, x, y, z);
}
static void norm3(float *v)
{
    float l = sqrtf(dot3(v, v));
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

// ---------------------------------------------------------------- camera
void view3d_setup(View *v, const Cam *c, int x, int y, int w, int h)
{
    float keep_cx = v->cx, keep_cy = v->cy, keep_s = v->cscale;
    int kw = v->canvas_w, kh = v->canvas_h;
    memset(v, 0, sizeof(*v));
    v->cx = keep_cx; v->cy = keep_cy; v->cscale = keep_s; v->canvas_w = kw; v->canvas_h = kh;
    v->x = x; v->y = y; v->w = w > 1 ? w : 1; v->h = h > 1 ? h : 1;
    v->cam = *c;
    float p = c->pitch * DEG, yw = c->yaw * DEG;
    float dir[3] = {cosf(p) * sinf(yw), sinf(p), cosf(p) * cosf(yw)};
    float d = c->ortho ? 400.0f : c->dist;
    for (int i = 0; i < 3; i++) {
        v->eye[i] = c->target[i] + dir[i] * d;
        v->fwd[i] = -dir[i];
    }
    float wup[3] = {0, 1, 0};
    cross3(v->right, v->fwd, wup);
    if (dot3(v->right, v->right) < 1e-6f) v3(v->right, cosf(yw), 0, -sinf(yw));
    norm3(v->right);
    cross3(v->up, v->right, v->fwd);
    norm3(v->up);
    v->tan_half = tanf(c->fov * 0.5f * DEG);
    v->aspect = (float)v->w / (float)v->h;
    v->ortho_h = c->dist * v->tan_half;
}

int world_to_screen(const View *v, const float p[3], float *sx, float *sy)
{
    float d[3] = {p[0] - v->eye[0], p[1] - v->eye[1], p[2] - v->eye[2]};
    float z = dot3(d, v->fwd);
    float nx, ny;
    if (v->cam.ortho) {
        nx = dot3(d, v->right) / (v->ortho_h * v->aspect);
        ny = dot3(d, v->up) / v->ortho_h;
    } else {
        if (z < 0.05f) return 0;
        nx = dot3(d, v->right) / (z * v->tan_half * v->aspect);
        ny = dot3(d, v->up) / (z * v->tan_half);
    }
    *sx = v->x + (nx * 0.5f + 0.5f) * v->w;
    *sy = v->y + (0.5f - ny * 0.5f) * v->h;
    return 1;
}

void screen_ray(const View *v, float sx, float sy, float org[3], float dir[3])
{
    float nx = ((sx - v->x) / v->w) * 2.0f - 1.0f;
    float ny = 1.0f - ((sy - v->y) / v->h) * 2.0f;
    if (v->cam.ortho) {
        for (int i = 0; i < 3; i++) {
            org[i] = v->eye[i] + v->right[i] * nx * v->ortho_h * v->aspect + v->up[i] * ny * v->ortho_h;
            dir[i] = v->fwd[i];
        }
        return;
    }
    for (int i = 0; i < 3; i++) {
        org[i] = v->eye[i];
        dir[i] = v->fwd[i] + v->right[i] * nx * v->tan_half * v->aspect + v->up[i] * ny * v->tan_half;
    }
    norm3(dir);
}

static void apply_camera(const View *v)
{
    r_matrix_mode(R_MATRIX_PROJECTION);
    r_load_identity();
    if (v->cam.ortho) {
        RMat4 m;
        memset(&m, 0, sizeof(m));
        float n = 1.0f, f = 1000.0f;
        m.m[0] = 1.0f / (v->ortho_h * v->aspect);
        m.m[5] = 1.0f / v->ortho_h;
        m.m[10] = -2.0f / (f - n);
        m.m[14] = -(f + n) / (f - n);
        m.m[15] = 1.0f;
        r_load_matrix(&m);
        r_set_near(1.0f);
    } else {
        r_perspective(v->cam.fov, v->aspect, 0.1f, 600.0f);
        r_set_near(0.1f);
    }
    r_matrix_mode(R_MATRIX_VIEW);
    r_load_identity();
    RVec3 eye = {v->eye[0], v->eye[1], v->eye[2]};
    RVec3 at = {v->eye[0] + v->fwd[0], v->eye[1] + v->fwd[1], v->eye[2] + v->fwd[2]};
    RVec3 up = {v->up[0], v->up[1], v->up[2]};
    r_look_at(&eye, &at, &up);
    r_matrix_mode(R_MATRIX_MODEL);
    r_load_identity();
}

static void inset_begin(const View *v, unsigned int fog_rgb, float fog_near, float fog_far, int fog, int clear)
{
    r_inset_3d(1);
    gfx_viewport(v->x, v->y, v->w, v->h);
    apply_camera(v);
    r_set_depth_test(1);
    r_set_depth_write(1);
    r_set_cull(0);
    if (clear) r_clear_depth(0);
    unsigned int abgr = 0xFF000000u | (fog_rgb & 0xFF) << 16 | (fog_rgb & 0xFF00) | ((fog_rgb >> 16) & 0xFF);
    r_set_fog(fog && !v->cam.ortho);
    r_set_fog_params(fog_near, fog_far, abgr);
}

static void inset_end(void)
{
    r_set_fog(0);
    r_set_depth_test(0);
    r_set_depth_write(0);
    gfx_viewport_screen();
    r_inset_3d(0);
}

// ---------------------------------------------------------------- primitives
typedef struct { float p[3], n[3]; } PV;
#define PRIM_MAX 600
typedef struct { PV v[PRIM_MAX]; int n; } Mesh;
enum { P_BOX, P_PLANE, P_SPHERE, P_CYL, P_CONE, P_OCTA, P_PYRAMID, P_COUNT };
static Mesh s_mesh[P_COUNT];

static void tri(Mesh *m, const float *a, const float *b, const float *c)
{
    if (m->n + 3 > PRIM_MAX) return;
    float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]}, n[3];
    cross3(n, e1, e2);
    norm3(n);
    const float *pts[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        memcpy(m->v[m->n].p, pts[i], sizeof(float) * 3);
        memcpy(m->v[m->n].n, n, sizeof(n));
        m->n++;
    }
}

static void quad(Mesh *m, const float *a, const float *b, const float *c, const float *d)
{
    tri(m, a, b, c);
    tri(m, a, c, d);
}

static void build_meshes(void)
{
    if (s_mesh[P_BOX].n) return;
    float c[8][3];
    for (int i = 0; i < 8; i++) v3(c[i], (i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
    Mesh *b = &s_mesh[P_BOX];
    quad(b, c[0], c[2], c[3], c[1]);  // -z
    quad(b, c[4], c[5], c[7], c[6]);  // +z
    quad(b, c[0], c[4], c[6], c[2]);  // -x
    quad(b, c[1], c[3], c[7], c[5]);  // +x
    quad(b, c[2], c[6], c[7], c[3]);  // +y
    quad(b, c[0], c[1], c[5], c[4]);  // -y
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
        float t0[3] = {b0[0], 0.5f, b0[2]}, t1[3] = {b1[0], 0.5f, b1[2]};
        float top[3] = {0, 0.5f, 0}, bot[3] = {0, -0.5f, 0};
        quad(&s_mesh[P_CYL], b0, t0, t1, b1);
        tri(&s_mesh[P_CYL], top, t1, t0);
        tri(&s_mesh[P_CYL], bot, b0, b1);
        tri(&s_mesh[P_CONE], b0, top, b1);
        tri(&s_mesh[P_CONE], bot, b0, b1);
    }
    float o[6][3] = {{0.5f, 0, 0}, {-0.5f, 0, 0}, {0, 0.5f, 0}, {0, -0.5f, 0}, {0, 0, 0.5f}, {0, 0, -0.5f}};
    int f[8][3] = {{0, 2, 4}, {4, 2, 1}, {1, 2, 5}, {5, 2, 0}, {4, 3, 0}, {1, 3, 4}, {5, 3, 1}, {0, 3, 5}};
    for (int i = 0; i < 8; i++) tri(&s_mesh[P_OCTA], o[f[i][0]], o[f[i][1]], o[f[i][2]]);
    float ap[3] = {0, 0, 0.5f}, q0[3] = {-0.4f, -0.3f, -0.5f}, q1[3] = {0.4f, -0.3f, -0.5f}, q2[3] = {0.4f, 0.3f, -0.5f}, q3[3] = {-0.4f, 0.3f, -0.5f};
    tri(&s_mesh[P_PYRAMID], ap, q1, q0);
    tri(&s_mesh[P_PYRAMID], ap, q2, q1);
    tri(&s_mesh[P_PYRAMID], ap, q3, q2);
    tri(&s_mesh[P_PYRAMID], ap, q0, q3);
    quad(&s_mesh[P_PYRAMID], q0, q1, q2, q3);
}

static int prim_of(const char *type)
{
    if (!strcmp(type, "plane")) return P_PLANE;
    if (!strcmp(type, "sphere")) return P_SPHERE;
    if (!strcmp(type, "cylinder")) return P_CYL;
    if (!strcmp(type, "cone")) return P_CONE;
    if (!strcmp(type, "light") || !strcmp(type, "marker")) return P_OCTA;
    if (!strcmp(type, "camera")) return P_PYRAMID;
    return P_BOX;
}

// ---------------------------------------------------------------- object transforms
typedef struct { float pos[3], r[9], scale[3]; } Xf;

static void xf_of(const Json *o, Xf *x)
{
    float rot[3] = {0, 0, 0};
    v3(x->pos, 0, 0, 0);
    v3(x->scale, 1, 1, 1);
    json_getv(o, "pos", x->pos, 3);
    json_getv(o, "rot", rot, 3);
    json_getv(o, "scale", x->scale, 3);
    const char *t = json_gets(o, "type", "box");
    if (!strcmp(t, "light") || !strcmp(t, "marker")) v3(x->scale, 0.4f, 0.4f, 0.4f);
    if (!strcmp(t, "camera")) v3(x->scale, 0.6f, 0.6f, 0.6f);
    float cx = cosf(rot[0] * DEG), sx = sinf(rot[0] * DEG);
    float cy = cosf(rot[1] * DEG), sy = sinf(rot[1] * DEG);
    float cz = cosf(rot[2] * DEG), sz = sinf(rot[2] * DEG);
    // R = Ry * Rx * Rz (yaw, then pitch, then roll), row-major.
    float ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    float rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx};
    float rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
    float t1[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            t1[i * 3 + j] = 0;
            for (int k = 0; k < 3; k++) t1[i * 3 + j] += ry[i * 3 + k] * rx[k * 3 + j];
        }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            x->r[i * 3 + j] = 0;
            for (int k = 0; k < 3; k++) x->r[i * 3 + j] += t1[i * 3 + k] * rz[k * 3 + j];
        }
}

static void xf_point(const Xf *x, const float *p, float *o)
{
    float s[3] = {p[0] * x->scale[0], p[1] * x->scale[1], p[2] * x->scale[2]};
    for (int i = 0; i < 3; i++) o[i] = x->pos[i] + x->r[i * 3] * s[0] + x->r[i * 3 + 1] * s[1] + x->r[i * 3 + 2] * s[2];
}

static void xf_dir(const Xf *x, const float *n, float *o)
{
    float s[3] = {n[0] / x->scale[0], n[1] / x->scale[1], n[2] / x->scale[2]};
    for (int i = 0; i < 3; i++) o[i] = x->r[i * 3] * s[0] + x->r[i * 3 + 1] * s[1] + x->r[i * 3 + 2] * s[2];
    norm3(o);
}

// ---------------------------------------------------------------- models (.mcm)
typedef struct { char path[128]; Model m; int ok; } ModelSlot;
static ModelSlot s_models[32];

static const Model *get_model(const char *path)
{
    if (!path || !*path) return NULL;
    for (int i = 0; i < 32; i++)
        if (s_models[i].path[0] && !strcmp(s_models[i].path, path)) return s_models[i].ok ? &s_models[i].m : NULL;
    for (int i = 0; i < 32; i++)
        if (!s_models[i].path[0]) {
            snprintf(s_models[i].path, sizeof(s_models[i].path), "%s", path);
            s_models[i].ok = model_load(&s_models[i].m, path) == 0;
            if (!s_models[i].ok) ed_log(LOG_WARN, "model %s: not found under assets/", path);
            return s_models[i].ok ? &s_models[i].m : NULL;
        }
    return NULL;
}

static void local_bounds(const Json *o, float lo[3], float hi[3])
{
    const char *t = json_gets(o, "type", "box");
    v3(lo, -0.5f, -0.5f, -0.5f);
    v3(hi, 0.5f, 0.5f, 0.5f);
    if (!strcmp(t, "plane")) { lo[1] = -0.02f; hi[1] = 0.02f; }
    if (!strcmp(t, "model")) {
        const Model *m = get_model(json_gets(o, "model", NULL));
        if (m) { memcpy(lo, m->file->bounds_min, 12); memcpy(hi, m->file->bounds_max, 12); }
    }
}

// ---------------------------------------------------------------- drawing
static unsigned int to_abgr(unsigned int rgb, float k)
{
    int r = (int)(((rgb >> 16) & 0xFF) * k), g = (int)(((rgb >> 8) & 0xFF) * k), b = (int)((rgb & 0xFF) * k);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000u | (unsigned)b << 16 | (unsigned)g << 8 | (unsigned)r;
}

static RVertex s_buf[PRIM_MAX];

static void draw_object(const Json *o, float dim)
{
    const char *t = json_gets(o, "type", "box");
    unsigned int rgb = parse_color(json_gets(o, "color", NULL), 0x8A8F93);
    Xf x;
    xf_of(o, &x);
    if (!strcmp(t, "model")) {
        const Model *m = get_model(json_gets(o, "model", NULL));
        if (m) {
            float rot[3] = {0, 0, 0};
            json_getv(o, "rot", rot, 3);
            r_matrix_mode(R_MATRIX_MODEL);
            r_load_identity();
            RVec3 p = {x.pos[0], x.pos[1], x.pos[2]}, s = {x.scale[0], x.scale[1], x.scale[2]};
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
        // Missing model: a box stands in.
    }
    const Mesh *m = &s_mesh[prim_of(t)];
    static const float L[3] = {0.42f, 0.81f, 0.40f};
    int emissive = !strcmp(t, "light") || !strcmp(t, "marker");
    for (int i = 0; i < m->n; i++) {
        float wp[3], wn[3];
        xf_point(&x, m->v[i].p, wp);
        xf_dir(&x, m->v[i].n, wn);
        float k = emissive ? 1.0f : 0.42f + 0.58f * fmaxf(0.0f, dot3(wn, L));
        s_buf[i] = (RVertex){wp[0], wp[1], wp[2], 0, 0, to_abgr(rgb, k * dim)};
    }
    r_bind_texture(NULL);
    r_draw(R_PRIM_TRIANGLES, s_buf, m->n);
}

static void line3(const float *a, const float *b, unsigned int rgb)
{
    RVertex v[2] = {{a[0], a[1], a[2], 0, 0, to_abgr(rgb, 1)}, {b[0], b[1], b[2], 0, 0, to_abgr(rgb, 1)}};
    r_bind_texture(NULL);
    r_draw(R_PRIM_LINES, v, 2);
}

static void draw_grid(const View *v)
{
    int plane = 0;  // 0 = XZ, 1 = XY (front/back), 2 = ZY (sides)
    if (v->cam.ortho && fabsf(v->cam.pitch) < 1.0f) {
        float ay = fmodf(fabsf(v->cam.yaw), 180.0f);
        plane = (ay < 45.0f || ay > 135.0f) ? 1 : 2;
    }
    int n = 20;
    float step = 1.0f;
    if (v->cam.dist > 60) step = 5.0f, n = 40;
    float c[3] = {roundf(v->cam.target[0] / step) * step, 0, roundf(v->cam.target[2] / step) * step};
    for (int i = -n; i <= n; i++) {
        float a[3], b[3], e[3], f[3];
        float o = i * step;
        if (plane == 0) {
            v3(a, c[0] + o, 0.02f, c[2] - n * step); v3(b, c[0] + o, 0.02f, c[2] + n * step);
            v3(e, c[0] - n * step, 0.02f, c[2] + o); v3(f, c[0] + n * step, 0.02f, c[2] + o);
        } else if (plane == 1) {
            v3(a, c[0] + o, -n * step, 0); v3(b, c[0] + o, n * step, 0);
            v3(e, c[0] - n * step, o, 0); v3(f, c[0] + n * step, o, 0);
        } else {
            v3(a, 0, -n * step, c[2] + o); v3(b, 0, n * step, c[2] + o);
            v3(e, 0, o, c[2] - n * step); v3(f, 0, o, c[2] + n * step);
        }
        unsigned int col = (i % 5 == 0) ? 0x5C6067 : 0x4A4D53;
        line3(a, b, col);
        line3(e, f, col);
    }
    float o0[3] = {0, 0.03f, 0}, ox[3] = {n * step, 0.03f, 0}, oz[3] = {0, 0.03f, n * step}, oy[3] = {0, 3, 0};
    line3(o0, ox, 0xC0504A);
    line3(o0, oz, 0x4A78C0);
    line3(o0, oy, 0x6FA85A);
}

static void wire_bounds(const Json *o, unsigned int rgb)
{
    Xf x;
    xf_of(o, &x);
    float lo[3], hi[3], c[8][3];
    local_bounds(o, lo, hi);
    for (int i = 0; i < 8; i++) {
        float p[3] = {(i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1], (i & 4) ? hi[2] : lo[2]};
        xf_point(&x, p, c[i]);
    }
    static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int i = 0; i < 12; i++) line3(c[E[i][0]], c[E[i][1]], rgb);
}

static void scene_fog(Json *scene, unsigned int *rgb, float *n, float *f, int *on)
{
    Json *set = json_get(scene, "settings");
    float fog[2] = {20, 80};
    json_getv(set, "fog", fog, 2);
    *n = fog[0];
    *f = fog[1];
    *rgb = parse_color(json_gets(set, "fog_color", NULL), 0x34383C);
    *on = json_getb(set, "fog_enabled", 0);
}

static int s_depth_clear = 1;
void view3d_new_frame(void) { s_depth_clear = 1; }

void draw_scene_3d(const View *v, Json *scene, int dim)
{
    build_meshes();
    unsigned int fog_rgb;
    float fn, ff;
    int fog;
    scene_fog(scene, &fog_rgb, &fn, &ff, &fog);
    // Layers share one depth buffer, so a lower layer's walls hide what stands behind them.
    inset_begin(v, fog_rgb, fn, ff, fog, s_depth_clear);
    s_depth_clear = 0;
    Json *objs = json_get(scene, "objects");
    for (Json *o = objs ? objs->child : NULL; o; o = o->next) {
        if (!json_getb(o, "visible", 1)) continue;
        draw_object(o, dim ? 0.6f : 1.0f);
    }
    inset_end();
}

// The active scene's editor overlay: grid under, selection box over (no fog).
static void draw_overlay_3d(const View *v, Json *scene)
{
    // After the scene, depth-tested: the grid shows on the ground, never through objects.
    inset_begin(v, 0, 0, 1, 0, s_depth_clear);
    s_depth_clear = 0;
    r_set_depth_write(0);
    draw_grid(v);
    Json *sel = find_object(scene, ed.sel);
    if (sel) {
        r_set_depth_test(0);
        wire_bounds(sel, C_ACCENT);
    }
    // Lights and cameras get a small wire hint: a drop line to the ground.
    Json *objs = json_get(scene, "objects");
    for (Json *o = objs ? objs->child : NULL; o; o = o->next) {
        const char *t = json_gets(o, "type", "");
        if (strcmp(t, "light") && strcmp(t, "marker")) continue;
        float p[3] = {0, 0, 0};
        json_getv(o, "pos", p, 3);
        float g[3] = {p[0], 0, p[2]};
        line3(p, g, 0x6A6E75);
    }
    inset_end();
}

// ---------------------------------------------------------------- 2D canvas
// Canvas zoom steps: fractions to fit large canvases, whole numbers to inspect pixels.
static const float ZOOMS[] = {0.125f, 0.25f, 0.333f, 0.5f, 0.667f, 0.75f, 1, 1.5f, 2, 3, 4, 6, 8};
#define NZOOM ((int)(sizeof(ZOOMS) / sizeof(ZOOMS[0])))

void view2d_fit(View *v, Json *scene, int x, int y, int w, int h, float zoom, float panx, float pany)
{
    float c[2] = {1280, 720};
    json_getv(json_get(scene, "settings"), "canvas", c, 2);
    v->x = x; v->y = y; v->w = w; v->h = h;
    v->canvas_w = (int)c[0];
    v->canvas_h = (int)c[1];
    float ratio = fminf((w - 24) / c[0], (h - 24) / c[1]), fit = ZOOMS[0];
    for (int i = 0; i < NZOOM; i++) if (ZOOMS[i] <= ratio) fit = ZOOMS[i];
    float s = zoom > 0 ? zoom : fit;
    v->cscale = s;
    v->cx = floorf(x + (w - c[0] * s) * 0.5f + panx);
    v->cy = floorf(y + (h - c[1] * s) * 0.5f + pany);
}

static void canvas_rect(const View *v, const float r[4], int *x, int *y, int *w, int *h)
{
    *x = (int)floorf(v->cx + r[0] * v->cscale);
    *y = (int)floorf(v->cy + r[1] * v->cscale);
    *w = (int)floorf(r[2] * v->cscale);
    *h = (int)floorf(r[3] * v->cscale);
}

void draw_scene_2d(const View *v, Json *scene, int editing)
{
    (void)editing;
    Json *objs = json_get(scene, "objects");
    int sc = (int)v->cscale;
    if (sc < 1) sc = 1;
    for (Json *o = objs ? objs->child : NULL; o; o = o->next) {
        if (!json_getb(o, "visible", 1)) continue;
        float r[4] = {0, 0, 40, 16};
        json_getv(o, "rect", r, 4);
        int x, y, w, h;
        canvas_rect(v, r, &x, &y, &w, &h);
        const char *t = json_gets(o, "type", "rect");
        unsigned int col = parse_color(json_gets(o, "color", NULL), 0x262B2E);
        const char *text = json_gets(o, "text", "");
        if (!strcmp(t, "rect")) gui_rect(x, y, w, h, col);
        else if (!strcmp(t, "panel")) {
            gui_rect(x, y, w, h, col);
            gui_frame(x, y, w, h, 0x50545B);
        } else if (!strcmp(t, "button")) {
            gui_rect(x, y, w, h, col);
            gui_frame(x, y, w, h, 0x7A7F87);
            int tw = font_width(text) * sc;
            font_draw_scaled(x + (w - tw) / 2, y + (h - font_cell_h() * sc) / 2, 0xE6E6E6, text, -1, sc);
        } else if (!strcmp(t, "text")) {
            gui_clip(x, y, w > 0 ? w : 1, h > 0 ? h : 1);
            font_draw_scaled(x, y, col, text, -1, sc);
            gui_unclip();
        } else {  // image placeholder
            gui_rect(x, y, w, h, 0x2A2C30);
            gui_frame(x, y, w, h, 0x4A5257);
            gui_line((float)x, (float)y, (float)(x + w - 1), (float)(y + h - 1), 0x3C3F45);
            gui_line((float)(x + w - 1), (float)y, (float)x, (float)(y + h - 1), 0x3C3F45);
            font_draw(x + 3, y + 2, C_DIM, json_gets(o, "image", "image"));
        }
    }
}

// ---------------------------------------------------------------- picking
static int ray_box(const float *o, const float *d, const float *lo, const float *hi, float *t_out)
{
    float t0 = -1e30f, t1 = 1e30f;
    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-8f) {
            if (o[i] < lo[i] || o[i] > hi[i]) return 0;
            continue;
        }
        float a = (lo[i] - o[i]) / d[i], b = (hi[i] - o[i]) / d[i];
        if (a > b) { float t = a; a = b; b = t; }
        if (a > t0) t0 = a;
        if (b < t1) t1 = b;
        if (t0 > t1) return 0;
    }
    if (t1 < 0) return 0;
    *t_out = t0 > 0 ? t0 : t1;
    return 1;
}

int pick_object(const View *v, Json *scene, float sx, float sy, char *out_id, int size, float hit[3])
{
    float org[3], dir[3];
    if (scene_is_2d(scene)) {
        // Canvas pixels: topmost (last drawn) rect under the point.
        Json *objs = json_get(scene, "objects");
        const char *best = NULL;
        float cx = (sx - v->cx) / v->cscale, cy = (sy - v->cy) / v->cscale;
        for (Json *o = objs ? objs->child : NULL; o; o = o->next) {
            float r[4] = {0, 0, 0, 0};
            json_getv(o, "rect", r, 4);
            if (!json_getb(o, "visible", 1)) continue;
            if (cx >= r[0] && cy >= r[1] && cx < r[0] + r[2] && cy < r[1] + r[3]) best = json_gets(o, "id", NULL);
        }
        if (hit) v3(hit, cx, cy, 0);
        if (!best) return 0;
        snprintf(out_id, (size_t)size, "%s", best);
        return 1;
    }
    screen_ray(v, sx, sy, org, dir);
    float best_t = 1e30f;
    const char *best = NULL;
    Json *objs = json_get(scene, "objects");
    for (Json *o = objs ? objs->child : NULL; o; o = o->next) {
        if (!json_getb(o, "visible", 1)) continue;
        Xf x;
        xf_of(o, &x);
        float lo[3], hi[3];
        local_bounds(o, lo, hi);
        // Ray into object space: R^T (p - pos) / scale.
        float lo_[3], ld[3], rel[3] = {org[0] - x.pos[0], org[1] - x.pos[1], org[2] - x.pos[2]};
        for (int i = 0; i < 3; i++) {
            lo_[i] = (x.r[i] * rel[0] + x.r[3 + i] * rel[1] + x.r[6 + i] * rel[2]) / x.scale[i];
            ld[i] = (x.r[i] * dir[0] + x.r[3 + i] * dir[1] + x.r[6 + i] * dir[2]) / x.scale[i];
        }
        float t;
        if (ray_box(lo_, ld, lo, hi, &t) && t < best_t) {
            // Big ground planes lose to anything standing on them.
            if (!strcmp(json_gets(o, "type", ""), "plane") && best) continue;
            best_t = t;
            best = json_gets(o, "id", NULL);
        }
    }
    if (best) {
        if (hit) for (int i = 0; i < 3; i++) hit[i] = org[i] + dir[i] * best_t;
        snprintf(out_id, (size_t)size, "%s", best);
        return 1;
    }
    // Nothing hit: the ground plane y = 0 still gives a world point.
    if (hit && fabsf(dir[1]) > 1e-5f) {
        float t = -org[1] / dir[1];
        if (t > 0) for (int i = 0; i < 3; i++) hit[i] = org[i] + dir[i] * t;
    }
    return 0;
}

void objects_in_rect(const View *v, Json *scene, float x0, float y0, float x1, float y1, Json *arr)
{
    Json *objs = json_get(scene, "objects");
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { float t = y0; y0 = y1; y1 = t; }
    for (Json *o = objs ? objs->child : NULL; o; o = o->next) {
        float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
        if (scene_is_2d(scene)) {
            float r[4] = {0, 0, 0, 0};
            json_getv(o, "rect", r, 4);
            bx0 = v->cx + r[0] * v->cscale; by0 = v->cy + r[1] * v->cscale;
            bx1 = bx0 + r[2] * v->cscale; by1 = by0 + r[3] * v->cscale;
            if (v->cscale <= 0) continue;
        } else {
            Xf x;
            xf_of(o, &x);
            float lo[3], hi[3];
            local_bounds(o, lo, hi);
            int any = 0;
            for (int i = 0; i < 8; i++) {
                float p[3] = {(i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1], (i & 4) ? hi[2] : lo[2]}, w[3], sx, sy;
                xf_point(&x, p, w);
                if (!world_to_screen(v, w, &sx, &sy)) continue;
                any = 1;
                if (sx < bx0) bx0 = sx;
                if (sy < by0) by0 = sy;
                if (sx > bx1) bx1 = sx;
                if (sy > by1) by1 = sy;
            }
            if (!any) continue;
            // A ground plane covers everything; only count it when the shape is mostly on it.
            if (!strcmp(json_gets(o, "type", ""), "plane") && (bx1 - bx0) > (x1 - x0) * 3) continue;
        }
        if (bx1 < x0 || bx0 > x1 || by1 < y0 || by0 > y1) continue;
        json_push(arr, json_str(json_gets(o, "id", "")));
    }
}

// ---------------------------------------------------------------- interaction
static struct {
    int dragging;          // 0 none, 1 body move, 2 axis move, 3 rotate, 4 scale, 5 2D move, 6 2D resize
    int axis;
    Json *orig;            // the object's fields before the drag (for one undo step)
    float start[3];
    float grab[3];
    int mx0, my0;
    float zoom2d, panx, pany;
    int view_drag;         // 1 orbit, 2 pan
} S = {0, 0, NULL, {0}, {0}, 0, 0, 0, 0, 0, 0};

static void begin_drag(Json *o, int kind)
{
    S.dragging = kind;
    json_free(S.orig);
    S.orig = json_clone(o);
    S.mx0 = gui.mx;
    S.my0 = gui.my;
}

static void end_drag(Json *scene, Json *o)
{
    // Put the original back, then apply the final fields as one logged operation.
    Json *fields = json_obj();
    static const char *const KEYS[] = {"pos", "rot", "scale", "rect"};
    for (int i = 0; i < 4; i++) {
        Json *now = json_get(o, KEYS[i]);
        Json *was = json_get(S.orig, KEYS[i]);
        if (now && !json_equal(now, was)) json_set(fields, KEYS[i], json_clone(now));
    }
    for (int i = 0; i < 4; i++) {
        Json *was = json_get(S.orig, KEYS[i]);
        if (was) json_set(o, KEYS[i], json_clone(was));
    }
    if (fields->child) {
        Json *a = json_obj();
        json_sets(a, "scene", scene_id(scene));
        json_sets(a, "id", json_gets(o, "id", ""));
        json_set(a, "fields", fields);
        ops_do("object.set", a);
    } else json_free(fields);
    json_free(S.orig);
    S.orig = NULL;
    S.dragging = 0;
}

static int ray_plane_y(const float *org, const float *dir, float y, float *out)
{
    if (fabsf(dir[1]) < 1e-5f) return 0;
    float t = (y - org[1]) / dir[1];
    if (t < 0) return 0;
    for (int i = 0; i < 3; i++) out[i] = org[i] + dir[i] * t;
    return 1;
}

static float snapf(float v, float step) { return roundf(v / step) * step; }

static int gizmo_axis_hit(const View *v, const float *pos, float len, int *axis)
{
    float sx, sy;
    if (!world_to_screen(v, pos, &sx, &sy)) return 0;
    for (int a = 0; a < 3; a++) {
        float e[3] = {pos[0], pos[1], pos[2]};
        e[a] += len;
        float ex, ey;
        if (!world_to_screen(v, e, &ex, &ey)) continue;
        float dx = ex - sx, dy = ey - sy, l2 = dx * dx + dy * dy;
        if (l2 < 4) continue;
        float t = ((gui.mx - sx) * dx + (gui.my - sy) * dy) / l2;
        if (t < 0.15f || t > 1.1f) continue;
        float px = sx + dx * t - gui.mx, py = sy + dy * t - gui.my;
        if (px * px + py * py < 25) { *axis = a; return 1; }
    }
    return 0;
}

static float gizmo_len(const View *v, const float *pos)
{
    if (v->cam.ortho) return v->ortho_h * 0.25f;
    float d[3] = {pos[0] - v->eye[0], pos[1] - v->eye[1], pos[2] - v->eye[2]};
    float z = dot3(d, v->fwd);
    return fmaxf(0.3f, z * v->tan_half * 0.28f);
}

static void draw_gizmo(const View *v, Json *o, int hover_axis)
{
    float pos[3] = {0, 0, 0};
    json_getv(o, "pos", pos, 3);
    float sx, sy, len = gizmo_len(v, pos);
    if (!world_to_screen(v, pos, &sx, &sy)) return;
    static const unsigned int COL[3] = {0xC8584A, 0x7EB05E, 0x5A84C8};
    if (ed.tool == TOOL_ROTATE) {
        float r = 46;
        gui_circle(sx, sy, r, S.dragging == 3 || hover_axis == 1 ? C_ACCENT : COL[1]);
        gui_circle(sx, sy, r + 1, 0x17181B);
        return;
    }
    for (int a = 0; a < 3; a++) {
        float e[3] = {pos[0], pos[1], pos[2]};
        e[a] += len;
        float ex, ey;
        if (!world_to_screen(v, e, &ex, &ey)) continue;
        unsigned int c = (hover_axis == a || (S.dragging && S.axis == a && S.dragging != 1)) ? C_ACCENT : COL[a];
        gui_line(sx, sy, ex, ey, c);
        gui_line(sx + 1, sy, ex + 1, ey, c);
        if (ed.tool == TOOL_SCALE) gui_rect((int)ex - 3, (int)ey - 3, 7, 7, c);
        else gui_disc(ex, ey, 3.5f, c);
    }
    gui_rect((int)sx - 2, (int)sy - 2, 5, 5, C_BRIGHT);
}

static void view_input_3d(View *v, Json *scene, Cam *cam)
{
    int over = gui_hit(v->x, v->y, v->w, v->h) && !gui.mouse_used && !gui.active;
    unsigned int b = gui.in.mouse_buttons;
    // Camera: RMB / Alt+LMB orbit, MMB / Shift+RMB pan, wheel dolly, RMB+WASD fly.
    if (over && (gui.in.mouse_pressed & 2)) S.view_drag = (input_mods() & MOD_SHIFT) ? 2 : 1;
    if (over && (gui.in.mouse_pressed & 4)) S.view_drag = 2;
    if (over && (gui.in.mouse_pressed & 1) && (input_mods() & MOD_ALT)) S.view_drag = 1;
    if (S.view_drag && !(b & 7)) S.view_drag = 0;
    float dx = gui.in.mouse_dx, dy = gui.in.mouse_dy;
    if (S.view_drag == 1 && (dx || dy)) {
        if (cam->ortho && (dx || dy)) cam->ortho = 0;  // orbiting leaves the preset views
        cam->yaw -= dx * 0.4f;
        cam->pitch += dy * 0.4f;
        if (cam->pitch > 89.5f) cam->pitch = 89.5f;
        if (cam->pitch < -89.5f) cam->pitch = -89.5f;
        ed.dirty = 1;
    } else if (S.view_drag == 2 && (dx || dy)) {
        float k = (cam->ortho ? v->ortho_h : cam->dist * v->tan_half) * 2.0f / v->h;
        for (int i = 0; i < 3; i++) cam->target[i] += (-v->right[i] * dx + v->up[i] * dy) * k;
        ed.dirty = 1;
    }
    if (S.view_drag == 1 && (b & 2)) {
        float mv[3] = {0, 0, 0}, sp = cam->dist * 0.9f * ed.dt * ((input_mods() & MOD_SHIFT) ? 3.0f : 1.0f);
        if (input_key_held(KEY_LETTER('W'))) for (int i = 0; i < 3; i++) mv[i] += v->fwd[i];
        if (input_key_held(KEY_LETTER('S'))) for (int i = 0; i < 3; i++) mv[i] -= v->fwd[i];
        if (input_key_held(KEY_LETTER('D'))) for (int i = 0; i < 3; i++) mv[i] += v->right[i];
        if (input_key_held(KEY_LETTER('A'))) for (int i = 0; i < 3; i++) mv[i] -= v->right[i];
        if (input_key_held(KEY_LETTER('E'))) mv[1] += 1;
        if (input_key_held(KEY_LETTER('Q'))) mv[1] -= 1;
        for (int i = 0; i < 3; i++) cam->target[i] += mv[i] * sp;
        if (mv[0] || mv[1] || mv[2]) ed.dirty = 1;
    }
    if (over && gui.in.wheel && !gui.wheel_used) {
        cam->dist *= powf(0.88f, (float)gui.in.wheel);
        if (cam->dist < 0.5f) cam->dist = 0.5f;
        if (cam->dist > 400) cam->dist = 400;
        gui.wheel_used = 1;
        ed.dirty = 1;
    }
    if (S.view_drag) { gui_take_mouse(); return; }

    // Object editing.
    if (json_getb(scene, "locked", 0)) return;
    Json *sel = find_object(scene, ed.sel);
    int edit_tool = ed.tool <= TOOL_SCALE;
    int hover_axis = -1;
    if (sel && edit_tool && !S.dragging) {
        float pos[3] = {0, 0, 0};
        json_getv(sel, "pos", pos, 3);
        if (ed.tool == TOOL_MOVE || ed.tool == TOOL_SCALE) {
            int a;
            if (gizmo_axis_hit(v, pos, gizmo_len(v, pos), &a)) hover_axis = a;
        } else if (ed.tool == TOOL_ROTATE) {
            float sx, sy;
            if (world_to_screen(v, pos, &sx, &sy)) {
                float d = sqrtf((gui.mx - sx) * (gui.mx - sx) + (gui.my - sy) * (gui.my - sy));
                if (fabsf(d - 46) < 6) hover_axis = 1;
            }
        }
    }
    if (over && gui.clicked && edit_tool && !(input_mods() & MOD_ALT)) {
        float org[3], dir[3];
        screen_ray(v, (float)gui.mx, (float)gui.my, org, dir);
        if (sel && hover_axis >= 0) {
            begin_drag(sel, ed.tool == TOOL_ROTATE ? 3 : (ed.tool == TOOL_SCALE ? 4 : 2));
            S.axis = hover_axis;
            json_getv(sel, ed.tool == TOOL_SCALE ? "scale" : "pos", S.start, 3);
            if (ed.tool == TOOL_ROTATE) json_getv(sel, "rot", S.start, 3);
        } else {
            char id[64];
            float hit[3];
            if (pick_object(v, scene, (float)gui.mx, (float)gui.my, id, sizeof(id), hit)) {
                snprintf(ed.sel, sizeof(ed.sel), "%s", id);
                ed.sel_note[0] = 0;
                sel = find_object(scene, id);
                if (sel && strcmp(json_gets(sel, "type", ""), "plane") && ed.tool != TOOL_ROTATE && ed.tool != TOOL_SCALE) {
                    begin_drag(sel, 1);
                    json_getv(sel, "pos", S.start, 3);
                    if (!ray_plane_y(org, dir, S.start[1], S.grab)) memcpy(S.grab, S.start, 12);
                }
            } else ed.sel[0] = 0;
        }
        gui_take_mouse();
    }
    if (S.dragging && sel) {
        if (!(b & 1)) { end_drag(scene, sel); return; }
        float org[3], dir[3];
        screen_ray(v, (float)gui.mx, (float)gui.my, org, dir);
        int snap = input_shortcut_mod();
        if (S.dragging == 1) {
            float p[3];
            if (ray_plane_y(org, dir, S.start[1], p)) {
                float np[3] = {S.start[0] + p[0] - S.grab[0], S.start[1], S.start[2] + p[2] - S.grab[2]};
                if (snap) { np[0] = snapf(np[0], 0.5f); np[2] = snapf(np[2], 0.5f); }
                json_setv(sel, "pos", np, 3);
            }
        } else if (S.dragging == 2 || S.dragging == 4) {
            float pos[3];
            memcpy(pos, S.start, 12);
            if (S.dragging == 4) json_getv(S.orig, "pos", pos, 3);
            float len = gizmo_len(v, pos), sx, sy, ex, ey, e[3] = {pos[0], pos[1], pos[2]};
            e[S.axis] += len;
            if (world_to_screen(v, pos, &sx, &sy) && world_to_screen(v, e, &ex, &ey)) {
                float ax = ex - sx, ay = ey - sy, l2 = ax * ax + ay * ay;
                if (l2 > 1) {
                    float t = ((gui.mx - S.mx0) * ax + (gui.my - S.my0) * ay) / l2 * len;
                    float nv[3];
                    memcpy(nv, S.start, 12);
                    if (S.dragging == 2) {
                        nv[S.axis] += t;
                        if (snap) nv[S.axis] = snapf(nv[S.axis], 0.5f);
                        json_setv(sel, "pos", nv, 3);
                    } else {
                        float k = 1.0f + t / len;
                        if (k < 0.01f) k = 0.01f;
                        if (input_mods() & MOD_SHIFT) for (int i = 0; i < 3; i++) nv[i] = S.start[i] * k;
                        else nv[S.axis] = S.start[S.axis] * k;
                        if (snap) for (int i = 0; i < 3; i++) nv[i] = snapf(nv[i], 0.25f);
                        json_setv(sel, "scale", nv, 3);
                    }
                }
            }
        } else if (S.dragging == 3) {
            float nv[3];
            memcpy(nv, S.start, 12);
            nv[1] = S.start[1] - (gui.mx - S.mx0) * 0.8f;
            if (snap) nv[1] = snapf(nv[1], 15.0f);
            nv[1] = fmodf(nv[1], 360.0f);
            json_setv(sel, "rot", nv, 3);
        }
        input_set_cursor(2);
        gui_take_mouse();
    }
    if (sel && edit_tool) draw_gizmo(v, sel, hover_axis);
}

static void view_input_2d(View *v, Json *scene)
{
    int over = gui_hit(v->x, v->y, v->w, v->h) && !gui.mouse_used && !gui.active;
    unsigned int b = gui.in.mouse_buttons;
    if (over && (gui.in.mouse_pressed & 6)) S.view_drag = 2;
    if (S.view_drag && !(b & 6)) S.view_drag = 0;
    if (S.view_drag == 2) {
        S.panx += gui.in.mouse_dx;
        S.pany += gui.in.mouse_dy;
        gui_take_mouse();
        return;
    }
    if (over && gui.in.wheel && !gui.wheel_used) {
        float z = S.zoom2d > 0 ? S.zoom2d : v->cscale;
        int k = 0;
        while (k < NZOOM - 1 && ZOOMS[k] < z - 0.001f) k++;
        k += gui.in.wheel > 0 ? 1 : -1;
        if (k < 0) k = 0;
        if (k >= NZOOM) k = NZOOM - 1;
        S.zoom2d = ZOOMS[k];
        gui.wheel_used = 1;
    }
    if (json_getb(scene, "locked", 0)) return;
    Json *sel = find_object(scene, ed.sel);
    int edit_tool = ed.tool <= TOOL_SCALE;
    // Selection frame and resize handle.
    int hx = 0, hy = 0, on_handle = 0;
    if (sel) {
        float r[4] = {0, 0, 0, 0};
        json_getv(sel, "rect", r, 4);
        int x, y, w, h;
        canvas_rect(v, r, &x, &y, &w, &h);
        gui_frame(x - 1, y - 1, w + 2, h + 2, C_ACCENT);
        hx = x + w - 3;
        hy = y + h - 3;
        on_handle = gui.mx >= hx - 2 && gui.mx < hx + 7 && gui.my >= hy - 2 && gui.my < hy + 7;
        gui_rect(hx, hy, 6, 6, on_handle ? C_BRIGHT : C_ACCENT);
        char tip[64];
        snprintf(tip, sizeof(tip), "%d,%d %dx%d", (int)r[0], (int)r[1], (int)r[2], (int)r[3]);
        font_draw(x, y - 15 > v->y ? y - 15 : y + h + 2, C_ACCENT, tip);
    }
    if (over && gui.clicked && edit_tool) {
        char id[64];
        if (sel && on_handle) begin_drag(sel, 6);
        else if (pick_object(v, scene, (float)gui.mx, (float)gui.my, id, sizeof(id), NULL)) {
            snprintf(ed.sel, sizeof(ed.sel), "%s", id);
            ed.sel_note[0] = 0;
            sel = find_object(scene, id);
            if (sel) begin_drag(sel, 5);
        } else ed.sel[0] = 0;
        if (sel && S.dragging) {
            float r[4];
            json_getv(sel, "rect", r, 4);
            S.start[0] = r[0]; S.start[1] = r[1]; S.grab[0] = r[2]; S.grab[1] = r[3];
        }
        gui_take_mouse();
    }
    if (S.dragging >= 5 && sel) {
        if (!(b & 1)) { end_drag(scene, sel); return; }
        float dx = (gui.mx - S.mx0) / v->cscale, dy = (gui.my - S.my0) / v->cscale;
        float r[4];
        json_getv(sel, "rect", r, 4);
        if (S.dragging == 5) { r[0] = roundf(S.start[0] + dx); r[1] = roundf(S.start[1] + dy); }
        else { r[2] = fmaxf(1, roundf(S.grab[0] + dx)); r[3] = fmaxf(1, roundf(S.grab[1] + dy)); }
        if (input_shortcut_mod()) for (int i = 0; i < 4; i++) r[i] = snapf(r[i], 4);
        json_setv(sel, "rect", r, 4);
        gui_take_mouse();
    }
}

void view_frame_selected(void)
{
    Json *s = cur_scene();
    if (!s) return;
    if (scene_is_2d(s)) { S.zoom2d = 0; S.panx = S.pany = 0; return; }
    Cam *c = &ed.cams[ed.cur];
    Json *o = find_object(s, ed.sel);
    if (!o) { cam_default(c); return; }
    float p[3] = {0, 0, 0}, sc[3] = {1, 1, 1};
    json_getv(o, "pos", p, 3);
    json_getv(o, "scale", sc, 3);
    memcpy(c->target, p, 12);
    float big = fmaxf(sc[0], fmaxf(sc[1], sc[2]));
    c->dist = fmaxf(2.5f, big * 2.6f);
    ed.dirty = 1;
}

// ---------------------------------------------------------------- the view
static const char *const VIEW_NAMES[] = {"view.persp", "view.top", "view.front", "view.right"};
static const char *const VIEW_PRESETS[] = {"persp", "top", "front", "right"};

static int view_buttons_width(void)
{
    int w = 0;
    for (int i = 0; i < 4; i++) {
        int bw = font_width(T(VIEW_NAMES[i])) + 12;
        w += (bw < 34 ? 34 : bw) + 2;
    }
    return w;
}

static void view_buttons(int x, int y, Cam *cam)
{
    int bx = x;
    for (int i = 0; i < 4; i++) {
        int on = i == 0 ? !cam->ortho
                        : (cam->ortho && ((i == 1 && cam->pitch > 80) || (i == 2 && fabsf(cam->pitch) < 1 && fabsf(cam->yaw) < 1) ||
                                          (i == 3 && fabsf(cam->pitch) < 1 && fabsf(cam->yaw - 90) < 1)));
        int bw = font_width(T(VIEW_NAMES[i])) + 12;
        if (bw < 34) bw = 34;
        if (gui_button_id(gui_id("viewp", i), T(VIEW_NAMES[i]), bx, y, bw, 16, on)) {
            Json *a = json_obj();
            json_sets(a, "preset", VIEW_PRESETS[i]);
            ops_do("view.set", a);
        }
        bx += bw + 2;
    }
}

static unsigned int scene_bg(Json *s)
{
    unsigned int rgb;
    float n, f;
    int on;
    scene_fog(s, &rgb, &n, &f, &on);
    return on ? rgb : parse_color(json_gets(json_get(s, "settings"), "background", NULL), 0x3A3D42);
}

void view3d_frame(int x, int y, int w, int h)
{
    Json *scene = cur_scene();
    gui_rect(x, y, w, h, 0x1B1C1F);
    if (!scene) {
        font_draw(x + 20, y + 20, C_DIM, T("view.no_scene"));
        return;
    }
    View *v = &ed.view;
    int is2d = scene_is_2d(scene);
    // Layers: the 3D scenes below draw with the active camera (or the topmost 3D
    // scene's camera when a 2D scene is being edited).
    int cam_i = ed.cur;
    if (is2d) {
        cam_i = -1;
        for (int i = 0; i < ed.nscenes; i++)
            if (!scene_is_2d(ed.scenes[i]) && json_getb(ed.scenes[i], "visible", 1)) cam_i = i;
    }
    if (is2d) {
        view2d_fit(v, scene, x, y, w, h, S.zoom2d, S.panx, S.pany);
        int cw = (int)(v->canvas_w * v->cscale), ch = (int)(v->canvas_h * v->cscale);
        gui_clip(x, y, w, h);
        gui_rect((int)v->cx - 1, (int)v->cy - 1, cw + 2, ch + 2, C_EDGE);
        gui_rect((int)v->cx, (int)v->cy, cw, ch, cam_i >= 0 ? scene_bg(ed.scenes[cam_i]) : 0x1B1C1F);
        if (cam_i >= 0) {
            View lv = *v;
            view3d_setup(&lv, &ed.cams[cam_i], (int)v->cx, (int)v->cy, cw, ch);
            view3d_new_frame();
            for (int i = 0; i < ed.nscenes; i++)
                if (!scene_is_2d(ed.scenes[i]) && json_getb(ed.scenes[i], "visible", 1)) draw_scene_3d(&lv, ed.scenes[i], 1);
        }
        for (int i = 0; i < ed.nscenes; i++) {
            Json *s = ed.scenes[i];
            if (!scene_is_2d(s) || (!json_getb(s, "visible", 1) && i != ed.cur)) continue;
            View lv = *v;
            draw_scene_2d(&lv, s, i == ed.cur);
        }
        gui_unclip();
        // Keep v->x/y as the canvas origin for notes (canvas pixels).
        gui_clip(x, y, w, h);
        view_input_2d(v, scene);
        notes_draw(v, scene, 0, ed.sel_note);
        notes_tool_input(v, x, y, w, h);
        gui_unclip();
        if (gui_hit(v->x, v->y, v->w, v->h)) {
            int cxp = (int)floorf((gui.mx - v->cx) / v->cscale), cyp = (int)floorf((gui.my - v->cy) / v->cscale);
            char t[64];
            snprintf(t, sizeof(t), "%d, %d   %d%%", cxp, cyp, (int)(v->cscale * 100 + 0.5f));
            font_draw(x + 6, y + h - 17, C_DIM, t);
        }
        return;
    }
    Cam *cam = &ed.cams[ed.cur];
    view3d_setup(v, cam, x, y, w, h);
    gui_rect(x, y, w, h, scene_bg(scene));
    view3d_new_frame();
    gui_clip(x, y, w, h);
    for (int i = 0; i < ed.nscenes; i++) {
        Json *s = ed.scenes[i];
        if (scene_is_2d(s)) continue;
        if (i != ed.cur && !json_getb(s, "visible", 1)) continue;
        draw_scene_3d(v, s, i != ed.cur);
    }
    draw_overlay_3d(v, scene);
    // Labels for lights and markers, which are small.
    Json *objs = json_get(scene, "objects");
    for (Json *o = objs ? objs->child : NULL; o; o = o->next) {
        const char *t = json_gets(o, "type", "");
        int is_sel = !strcmp(json_gets(o, "id", ""), ed.sel);
        if (!is_sel && strcmp(t, "light") && strcmp(t, "marker") && strcmp(t, "camera")) continue;
        float p[3] = {0, 0, 0}, sx, sy;
        json_getv(o, "pos", p, 3);
        if (world_to_screen(v, p, &sx, &sy))
            font_draw((int)sx + 8, (int)sy - 16, is_sel ? C_ACCENT : C_DIM, json_gets(o, "name", ""));
    }
    // 2D layers above the world, fitted like the game screen.
    for (int i = 0; i < ed.nscenes; i++) {
        Json *s = ed.scenes[i];
        if (!scene_is_2d(s) || !json_getb(s, "visible", 1)) continue;
        View lv;
        memset(&lv, 0, sizeof(lv));
        view2d_fit(&lv, s, x, y, w, h, 0, 0, 0);
        draw_scene_2d(&lv, s, 0);
    }
    view_input_3d(v, scene, cam);
    notes_draw(v, scene, 0, ed.sel_note);
    notes_tool_input(v, x, y, w, h);
    gui_unclip();
    view_buttons(x + w - view_buttons_width() - 4, y + 4, cam);
    char info[160];
    float hit[3] = {0, 0, 0};
    char pid[64] = "";
    if (gui_hit(x, y, w, h)) {
        pick_object(v, scene, (float)gui.mx, (float)gui.my, pid, sizeof(pid), hit);
        snprintf(info, sizeof(info), "%s  %.2f, %.2f, %.2f  %s", cam->ortho ? T("view.ortho") : T("view.persp"), hit[0], hit[1], hit[2], pid);
        font_draw(x + 6, y + h - 17, C_DIM, info);
    }
}
