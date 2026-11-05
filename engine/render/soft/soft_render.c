// Software rasteriser: 480x272 RGB565 colour, 16-bit reversed depth, no mipmaps,
// nearest sampling, per-vertex fog and 16-bit ordered dither (retro look).
#include "../render_api.h"
#include "engine/render/render_config.h"
#include "../../platform/platform.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// The frame size is set at run time (r_set_frame_size); games start at the retro size.
static int s_w = MC_LOWRES_W, s_h = MC_LOWRES_H, s_stride = MC_BUF_STRIDE;
#define SW s_w
#define SH s_h
#define SSTRIDE s_stride

static uint16_t *s_fbuf, *s_zbuf;
static int s_native;  // native look: no ordered dither
#define FB(y) (s_fbuf + (size_t)(y) * s_stride)
#define ZB(y) (s_zbuf + (size_t)(y) * s_stride)

static int alloc_frame(int w, int h)
{
    int stride = (w + 15) & ~15;
    uint16_t *f = (uint16_t *)calloc((size_t)stride * (size_t)h, sizeof(uint16_t));
    uint16_t *z = (uint16_t *)calloc((size_t)stride * (size_t)h, sizeof(uint16_t));
    if (!f || !z) { free(f); free(z); return -1; }
    free(s_fbuf);
    free(s_zbuf);
    s_fbuf = f;
    s_zbuf = z;
    s_w = w;
    s_h = h;
    s_stride = stride;
    return 0;
}

int soft_r_set_frame_size(int w, int h)
{
    if (w < 16 || h < 16 || w > 8192 || h > 8192) return -1;
    if (s_fbuf && w == s_w && h == s_h) return 0;
    return alloc_frame(w, h);
}

void soft_r_set_native(int on) { s_native = on; }
static int s_headless;
static float s_near = 0.5f;
static int s_near_set;

// ---------------------------------------------------------------- matrices
typedef struct {
    RMat4 m;
} Stack;
static Stack s_stack[3][8];
static int s_top[3];
static RMatrixMode s_mmode;

static void mat_identity(RMat4 *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

// result = a * b, column-major (same convention as sceGum).
static void mat_mul(RMat4 *r, const RMat4 *a, const RMat4 *b)
{
    RMat4 t;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float v = 0.0f;
            for (int k = 0; k < 4; k++)
                v += a->m[k * 4 + j] * b->m[i * 4 + k];
            t.m[i * 4 + j] = v;
        }
    *r = t;
}

static RMat4 *cur(void) { return &s_stack[s_mmode][s_top[s_mmode]].m; }

void soft_r_matrix_mode(RMatrixMode mode)
{
    if (mode >= 0 && mode <= 2)
        s_mmode = mode;
}
void soft_r_load_identity(void) { mat_identity(cur()); }
void soft_r_load_matrix(const RMat4 *m) { if (m) *cur() = *m; }
void soft_r_store_matrix(RMat4 *m) { if (m) *m = *cur(); }
void soft_r_update_matrix(void) {}

void soft_r_translate(const RVec3 *v)
{
    RMat4 t;
    mat_identity(&t);
    t.m[12] = v->x;
    t.m[13] = v->y;
    t.m[14] = v->z;
    mat_mul(cur(), cur(), &t);
}

void soft_r_rotate_x(float a)
{
    RMat4 t; mat_identity(&t);
    float c = cosf(a), s = sinf(a);
    t.m[5] = c; t.m[6] = s; t.m[9] = -s; t.m[10] = c;
    mat_mul(cur(), cur(), &t);
}
void soft_r_rotate_y(float a)
{
    RMat4 t; mat_identity(&t);
    float c = cosf(a), s = sinf(a);
    t.m[0] = c; t.m[2] = -s; t.m[8] = s; t.m[10] = c;
    mat_mul(cur(), cur(), &t);
}
void soft_r_rotate_z(float a)
{
    RMat4 t; mat_identity(&t);
    float c = cosf(a), s = sinf(a);
    t.m[0] = c; t.m[1] = s; t.m[4] = -s; t.m[5] = c;
    mat_mul(cur(), cur(), &t);
}
void soft_r_scale(const RVec3 *v)
{
    RMat4 *m = cur();
    m->m[0] *= v->x; m->m[1] *= v->x; m->m[2] *= v->x;
    m->m[4] *= v->y; m->m[5] *= v->y; m->m[6] *= v->y;
    m->m[8] *= v->z; m->m[9] *= v->z; m->m[10] *= v->z;
}

void soft_r_perspective(float fov_deg, float aspect, float near_m, float far_m)
{
    RMat4 t;
    mat_identity(&t);
    float angle = (fov_deg * 0.5f) * (3.14159265f / 180.0f);
    float cot = cosf(angle) / sinf(angle);
    float dz = near_m - far_m;
    t.m[0] = cot / aspect;
    t.m[5] = cot;
    t.m[10] = (far_m + near_m) / dz;
    t.m[11] = -1.0f;
    t.m[14] = 2.0f * (far_m * near_m) / dz;
    t.m[15] = 0.0f;
    mat_mul(cur(), cur(), &t);
    s_near = near_m;
    s_near_set = 1;
}

void soft_r_look_at(const RVec3 *eye, const RVec3 *at, const RVec3 *up)
{
    RVec3 f = {at->x - eye->x, at->y - eye->y, at->z - eye->z};
    float len = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
    if (len < 1e-8f) return;
    f.x /= len; f.y /= len; f.z /= len;
    RVec3 s = {f.y * up->z - f.z * up->y, f.z * up->x - f.x * up->z, f.x * up->y - f.y * up->x};
    len = sqrtf(s.x * s.x + s.y * s.y + s.z * s.z);
    if (len < 1e-8f) return;
    s.x /= len; s.y /= len; s.z /= len;
    RVec3 u = {s.y * f.z - s.z * f.y, s.z * f.x - s.x * f.z, s.x * f.y - s.y * f.x};
    RMat4 t;
    mat_identity(&t);
    t.m[0] = s.x;  t.m[4] = s.y;  t.m[8] = s.z;
    t.m[1] = u.x;  t.m[5] = u.y;  t.m[9] = u.z;
    t.m[2] = -f.x; t.m[6] = -f.y; t.m[10] = -f.z;
    mat_mul(cur(), cur(), &t);
    RVec3 ie = {-eye->x, -eye->y, -eye->z};
    soft_r_translate(&ie);
}

// ---------------------------------------------------------------- state
static int s_two_d;
static int s_fog_on = 1;
static float s_fog_near = 20.0f, s_fog_far = 80.0f;
static uint32_t s_fog_color = 0xFF3C3834u;
static int s_depth_test = 1, s_depth_write = 1, s_cull = 1, s_alpha_test = 0;
static RBlend s_blend = R_BLEND_NONE;
static RTexFunc s_tex_func = R_TEXFUNC_MODULATE;
static RWrap s_wrap = R_WRAP_REPEAT;
static float s_uv_su = 1.0f, s_uv_sv = 1.0f, s_uv_ou, s_uv_ov;
static RTexDesc s_tex;
static int s_tex_on;
static int s_vx, s_vy, s_vw = MC_LOWRES_W, s_vh = MC_LOWRES_H;
static int s_sx, s_sy, s_sw = MC_LOWRES_W, s_sh = MC_LOWRES_H;

// scratch vertex memory (sceGuGetMemory replacement)
static uint8_t *s_temp;
static int s_temp_cap, s_temp_used;

void soft_r_temp_reset(void) { s_temp_used = 0; }
void *soft_r_temp_alloc(int bytes)
{
    if (bytes <= 0) return NULL;
    if (!s_temp) {
        s_temp_cap = 4 * 1024 * 1024;
        s_temp = malloc((size_t)s_temp_cap);
    }
    if (!s_temp || s_temp_used + bytes > s_temp_cap)
        return NULL;
    void *p = s_temp + s_temp_used;
    s_temp_used += (bytes + 15) & ~15;
    return p;
}

void soft_r_size(int *w, int *h) { if (w) *w = SW; if (h) *h = SH; }
const char *soft_r_backend_name(void) { return "software"; }

int soft_r_init(const char *backend)
{
    (void)backend;
    if (!s_fbuf && alloc_frame(s_w, s_h) != 0) return -1;
    mat_identity(&s_stack[0][0].m);
    mat_identity(&s_stack[1][0].m);
    mat_identity(&s_stack[2][0].m);
    s_top[0] = s_top[1] = s_top[2] = 0;
    s_mmode = R_MATRIX_MODEL;
    s_headless = 0;
    soft_r_temp_reset();
    return 0;
}

void soft_r_shutdown(void)
{
    free(s_fbuf);
    free(s_zbuf);
    s_fbuf = s_zbuf = NULL;
    free(s_temp);
    s_temp = NULL;
    s_temp_cap = s_temp_used = 0;
}

void soft_r_begin_3d(void)
{
    s_two_d = 0;
    s_top[0] = s_top[1] = s_top[2] = 0;
    soft_r_temp_reset();
    s_vx = s_sx = 0;
    s_vy = s_sy = 0;
    s_vw = s_sw = SW;
    s_vh = s_sh = SH;
    s_tex_on = 0;
    s_uv_su = s_uv_sv = 1.0f;
    s_uv_ou = s_uv_ov = 0.0f;
    s_wrap = R_WRAP_REPEAT;
    s_tex_func = R_TEXFUNC_MODULATE;
    s_blend = R_BLEND_NONE;
    s_alpha_test = 0;
    s_depth_test = 1;
    s_depth_write = 1;
    s_cull = 1;
    s_fog_on = 1;
}

void soft_r_begin_2d(void)
{
    s_two_d = 1;
    s_vx = s_sx = 0;
    s_vy = s_sy = 0;
    s_vw = s_sw = SW;
    s_vh = s_sh = SH;
    s_depth_test = 0;
    s_depth_write = 0;
    s_cull = 0;
    s_fog_on = 0;
    s_blend = R_BLEND_NONE;
    s_alpha_test = 0;
    s_tex_on = 0;
    s_uv_su = s_uv_sv = 1.0f;
    s_uv_ou = s_uv_ov = 0.0f;
    s_wrap = R_WRAP_REPEAT;
    s_tex_func = R_TEXFUNC_MODULATE;
}

void soft_r_inset_3d(int on)
{
    if (on) s_two_d = 0;
    else soft_r_begin_2d();
}

void soft_r_end_frame(void) {}
void soft_r_present(void)
{
    if (!s_headless)
        plat_video_present_565(s_fbuf, SW, SH, SSTRIDE);
}

void soft_r_set_headless(int on) { s_headless = on; }

static uint16_t abgr_to_565(uint32_t c)
{
    unsigned int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    return (uint16_t)(((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3));
}

void soft_r_clear(uint32_t abgr, int clear_depth)
{
    uint16_t c = abgr_to_565(abgr);
    for (int y = 0; y < SH; y++) {
        uint16_t *row = FB(y);
        for (int x = 0; x < SSTRIDE; x++)
            row[x] = c;
    }
    if (clear_depth >= 0) {
        uint16_t d = (uint16_t)clear_depth;
        for (int y = 0; y < SH; y++)
            for (int x = 0; x < SSTRIDE; x++)
                ZB(y)[x] = d;
    }
}

void soft_r_clear_depth(int value)
{
    uint16_t d = (uint16_t)value;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SSTRIDE; x++)
            ZB(y)[x] = d;
}

void soft_r_viewport(int x, int y, int w, int h)
{
    s_vx = x; s_vy = y; s_vw = w; s_vh = h;
}
void soft_r_scissor(int x, int y, int w, int h)
{
    s_sx = x; s_sy = y; s_sw = w; s_sh = h;
}

void soft_r_set_fog(int on) { s_fog_on = on; }
void soft_r_set_fog_params(float near_m, float far_m, uint32_t abgr)
{
    s_fog_near = near_m; s_fog_far = far_m; s_fog_color = abgr;
}
void soft_r_set_depth_test(int on) { s_depth_test = on; }
void soft_r_set_depth_write(int on) { s_depth_write = on; }
void soft_r_set_cull(int on) { s_cull = on; }
void soft_r_set_alpha_test(int on) { s_alpha_test = on; }
void soft_r_set_blend(RBlend b) { s_blend = b; }
void soft_r_set_tex_func(RTexFunc f) { s_tex_func = f; }
void soft_r_set_tex_wrap(RWrap w) { s_wrap = w; }
void soft_r_set_uv_scale(float u, float v) { s_uv_su = u; s_uv_sv = v; }
void soft_r_set_uv_offset(float u, float v) { s_uv_ou = u; s_uv_ov = v; }
void soft_r_bind_texture(const RTexDesc *t)
{
    if (!t) { s_tex_on = 0; return; }
    s_tex = *t;
    s_tex_on = 1;
    s_wrap = t->wrap;
}

// ---------------------------------------------------------------- texture
static inline void sample_tex(float u, float v, unsigned int *out)
{
    const RTexDesc *t = &s_tex;
    if (t->format == R_TEX_T4) {
        if (s_wrap == R_WRAP_CLAMP) {
            if (u < 0.0f) u = 0.0f;
            if (u > 1.0f) u = 1.0f;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
        } else {
            u -= floorf(u);
            v -= floorf(v);
        }
        int x = (int)(u * t->w);
        int y = (int)(v * t->h);
        if (x >= t->w) x = t->w - 1;
        if (y >= t->h) y = t->h - 1;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        const uint8_t *px = (const uint8_t *)t->pixels;
        uint8_t b = px[y * ((t->w + 1) / 2) + x / 2];
        int idx = (x & 1) ? (b >> 4) : (b & 0xF);
        *out = t->clut ? t->clut[idx] : 0xFFFFFFFFu;
    } else {
        if (s_wrap == R_WRAP_CLAMP) {
            if (u < 0.0f) u = 0.0f;
            if (u > 1.0f) u = 1.0f;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
        } else {
            u -= floorf(u);
            v -= floorf(v);
        }
        int x = (int)(u * t->w), y = (int)(v * t->h);
        if (x >= t->w) x = t->w - 1;
        if (y >= t->h) y = t->h - 1;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        uint16_t c = ((const uint16_t *)t->pixels)[y * t->w + x];
        unsigned int r = (c & 0x1F) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c >> 11) << 3;
        *out = 0xFF000000u | (b << 16) | (g << 8) | r;
    }
}

// ---------------------------------------------------------------- raster
static const int BAYER[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}
};

static inline void put_pixel(int x, int y, uint16_t c)
{
    if (x < s_sx || y < s_sy || x >= s_sx + s_sw || y >= s_sy + s_sh)
        return;
    if (x < s_vx || y < s_vy || x >= s_vx + s_vw || y >= s_vy + s_vh)
        return;
    FB(y)[x] = c;
}

static inline uint16_t shade_pixel(unsigned int r, unsigned int g, unsigned int b, int x, int y)
{
    // Ordered dither into 5/6/5. This is part of the retro look, not an artefact;
    // the native look skips it.
    int d = s_native ? 0 : BAYER[y & 3][x & 3] - 8;
    int rr = (int)r + (d >> 1);
    int gg = (int)g + (d >> 2);
    int bb = (int)b + (d >> 1);
    if (rr < 0) rr = 0; if (rr > 255) rr = 255;
    if (gg < 0) gg = 0; if (gg > 255) gg = 255;
    if (bb < 0) bb = 0; if (bb > 255) bb = 255;
    return (uint16_t)(((bb >> 3) << 11) | ((gg >> 2) << 5) | (rr >> 3));
}

typedef struct {
    float x, y, z;        // screen
    float iw;             // 1/w
    float u, v;           // u/w, v/w
    float r, g, b, a;     // 0..255
    float fog;            // 0 = fog, 1 = clear
    float depth;          // 0..65535
} ClipVert;

static void blend_pixel(int x, int y, unsigned int r, unsigned int g, unsigned int b, unsigned int a)
{
    if (a >= 255 || s_blend == R_BLEND_NONE) {
        put_pixel(x, y, shade_pixel(r, g, b, x, y));
        return;
    }
    if (a == 0)
        return;
    uint16_t d = FB(y)[x];
    unsigned int dr = (d & 0x1F) << 3, dg = ((d >> 5) & 0x3F) << 2, db = (d >> 11) << 3;
    if (s_blend == R_BLEND_ADD) {
        r = r * a / 255 + dr;
        g = g * a / 255 + dg;
        b = b * a / 255 + db;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
    } else {
        r = (r * a + dr * (255 - a)) / 255;
        g = (g * a + dg * (255 - a)) / 255;
        b = (b * a + db * (255 - a)) / 255;
    }
    put_pixel(x, y, shade_pixel(r, g, b, x, y));
}

static inline float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void raster_tri(ClipVert a, ClipVert b, ClipVert c, const RTexDesc *tex, int textured, RTexFunc tf)
{
    float area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
    if (fabsf(area) < 1e-6f)
        return;
    if (s_cull && area < 0.0f)
        return;
    int neg = area < 0.0f;
    int minx = (int)floorf(fminf(a.x, fminf(b.x, c.x)));
    int maxx = (int)ceilf(fmaxf(a.x, fmaxf(b.x, c.x)));
    int miny = (int)floorf(fminf(a.y, fminf(b.y, c.y)));
    int maxy = (int)ceilf(fmaxf(a.y, fmaxf(b.y, c.y)));
    if (minx < s_sx) minx = s_sx;
    if (miny < s_sy) miny = s_sy;
    if (maxx > s_sx + s_sw - 1) maxx = s_sx + s_sw - 1;
    if (maxy > s_sy + s_sh - 1) maxy = s_sy + s_sh - 1;
    if (minx < s_vx) minx = s_vx;
    if (miny < s_vy) miny = s_vy;
    if (maxx > s_vx + s_vw - 1) maxx = s_vx + s_vw - 1;
    if (maxy > s_vy + s_vh - 1) maxy = s_vy + s_vh - 1;
    float inv = 1.0f / area;
    for (int y = miny; y <= maxy; y++) {
        float py = (float)y + 0.5f;
        for (int x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f;
            float w0 = edge(b.x, b.y, c.x, c.y, px, py);
            float w1 = edge(c.x, c.y, a.x, a.y, px, py);
            float w2 = edge(a.x, a.y, b.x, b.y, px, py);
            if (neg ? (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f)
                    : (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f))
                continue;
            w0 *= inv; w1 *= inv; w2 *= inv;
            float depth = a.depth * w0 + b.depth * w1 + c.depth * w2;
            if (s_depth_test) {
                if (depth < (float)ZB(y)[x])
                    continue;
            }
            unsigned int tr = 255, tg = 255, tb = 255, ta = 255;
            float vr = a.r * w0 + b.r * w1 + c.r * w2;
            float vg = a.g * w0 + b.g * w1 + c.g * w2;
            float vb = a.b * w0 + b.b * w1 + c.b * w2;
            float va = a.a * w0 + b.a * w1 + c.a * w2;
            if (textured && tex) {
                float iw = a.iw * w0 + b.iw * w1 + c.iw * w2;
                float u = (a.u * w0 + b.u * w1 + c.u * w2) / iw;
                float v = (a.v * w0 + b.v * w1 + c.v * w2) / iw;
                unsigned int tcol = 0xFFFFFFFFu;
                sample_tex(u, v, &tcol);
                tr = tcol & 0xFF; tg = (tcol >> 8) & 0xFF; tb = (tcol >> 16) & 0xFF; ta = (tcol >> 24) & 0xFF;
                if (s_alpha_test && ta == 0)
                    continue;
                if (tf == R_TEXFUNC_MODULATE) {
                    vr = vr * tr / 255.0f;
                    vg = vg * tg / 255.0f;
                    vb = vb * tb / 255.0f;
                    va = va * (float)ta / 255.0f;
                } else {
                    // REPLACE: texture colour, vertex alpha still modulates cut-outs.
                    vr = (float)tr; vg = (float)tg; vb = (float)tb;
                    va = va * (float)ta / 255.0f;
                }
            } else {
                if (s_alpha_test && va < 1.0f)
                    continue;
                tr = 255;
            }
            if (s_fog_on) {
                float f = a.fog * w0 + b.fog * w1 + c.fog * w2;
                if (f < 0.0f) f = 0.0f;
                if (f > 1.0f) f = 1.0f;
                float fr = (float)(s_fog_color & 0xFF), fg = (float)((s_fog_color >> 8) & 0xFF);
                float fb = (float)((s_fog_color >> 16) & 0xFF);
                vr = fr + (vr - fr) * f;
                vg = fg + (vg - fg) * f;
                vb = fb + (vb - fb) * f;
            }
            if (s_depth_test && s_depth_write)
                ZB(y)[x] = (uint16_t)(depth < 0 ? 0 : (depth > 65535 ? 65535 : depth));
            blend_pixel(x, y, (unsigned int)vr, (unsigned int)vg, (unsigned int)vb, (unsigned int)va);
        }
    }
}

static void draw_line(ClipVert a, ClipVert b)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    int n = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
    if (n <= 0) n = 1;
    if (n > 4096) n = 4096;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        int x = (int)(a.x + dx * t), y = (int)(a.y + dy * t);
        float depth = a.depth + (b.depth - a.depth) * t;
        if (x < s_sx || y < s_sy || x >= s_sx + s_sw || y >= s_sy + s_sh)
            continue;
        if (s_depth_test && depth < ZB(y)[x])
            continue;
        if (s_depth_test && s_depth_write)
            ZB(y)[x] = (uint16_t)depth;
        float r = a.r + (b.r - a.r) * t, g = a.g + (b.g - a.g) * t, bl = a.b + (b.b - a.b) * t;
        float al = a.a + (b.a - a.a) * t;
        blend_pixel(x, y, (unsigned int)r, (unsigned int)g, (unsigned int)bl, (unsigned int)al);
    }
}

// ---------------------------------------------------------------- pipeline
typedef struct { float x, y, z; float u, v; float r, g, b, a; float fog; } VView;

static void to_view_raw(float x, float y, float z, float u, float v, uint32_t color, VView *o)
{
    const RMat4 *m = &s_stack[R_MATRIX_MODEL][s_top[R_MATRIX_MODEL]].m;
    const RMat4 *vw = &s_stack[R_MATRIX_VIEW][s_top[R_MATRIX_VIEW]].m;
    float ax = m->m[0] * x + m->m[4] * y + m->m[8] * z + m->m[12];
    float ay = m->m[1] * x + m->m[5] * y + m->m[9] * z + m->m[13];
    float az = m->m[2] * x + m->m[6] * y + m->m[10] * z + m->m[14];
    o->x = vw->m[0] * ax + vw->m[4] * ay + vw->m[8] * az + vw->m[12];
    o->y = vw->m[1] * ax + vw->m[5] * ay + vw->m[9] * az + vw->m[13];
    o->z = vw->m[2] * ax + vw->m[6] * ay + vw->m[10] * az + vw->m[14];
    o->u = u * s_uv_su + s_uv_ou;
    o->v = v * s_uv_sv + s_uv_ov;
    o->r = (float)(color & 0xFF);
    o->g = (float)((color >> 8) & 0xFF);
    o->b = (float)((color >> 16) & 0xFF);
    o->a = (float)((color >> 24) & 0xFF);
    if (s_fog_on) {
        float d = -o->z;
        float f = (s_fog_far - d) / (s_fog_far - s_fog_near);
        o->fog = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    } else {
        o->fog = 1.0f;
    }
}

static void to_view(const RVertex *v, VView *o)
{
    to_view_raw(v->x, v->y, v->z, v->u, v->v, v->color, o);
}

static void project(VView v, ClipVert *o)
{
    const RMat4 *p = &s_stack[R_MATRIX_PROJECTION][s_top[R_MATRIX_PROJECTION]].m;
    float cx = p->m[0] * v.x + p->m[4] * v.y + p->m[8] * v.z + p->m[12];
    float cy = p->m[1] * v.x + p->m[5] * v.y + p->m[9] * v.z + p->m[13];
    float cz = p->m[2] * v.x + p->m[6] * v.y + p->m[10] * v.z + p->m[14];
    float cw = p->m[3] * v.x + p->m[7] * v.y + p->m[11] * v.z + p->m[15];
    if (cw < 1e-6f) cw = 1e-6f;
    float nx = cx / cw, ny = cy / cw, nz = cz / cw;
    o->x = (float)s_vx + (nx * 0.5f + 0.5f) * (float)s_vw;
    o->y = (float)s_vy + (0.5f - ny * 0.5f) * (float)s_vh;
    o->iw = 1.0f / cw;
    o->u = v.u * o->iw;
    o->v = v.v * o->iw;
    o->r = v.r; o->g = v.g; o->b = v.b; o->a = v.a;
    o->fog = v.fog;
    o->depth = (1.0f - nz) * 0.5f * 65535.0f;
}

static VView lerp3(VView a, VView b, float t)
{
    VView o;
    o.x = a.x + (b.x - a.x) * t; o.y = a.y + (b.y - a.y) * t; o.z = a.z + (b.z - a.z) * t;
    o.u = a.u + (b.u - a.u) * t; o.v = a.v + (b.v - a.v) * t;
    o.r = a.r + (b.r - a.r) * t; o.g = a.g + (b.g - a.g) * t;
    o.b = a.b + (b.b - a.b) * t; o.a = a.a + (b.a - a.a) * t;
    o.fog = a.fog + (b.fog - a.fog) * t;
    return o;
}

static void clip_and_draw(VView a, VView b, VView c)
{
    // Near-plane clip in view space before the perspective divide. Without this a
    // triangle crossing the eye plane stretches across the screen.
    float near = s_near_set ? s_near : 0.05f;
    float zlim = -near;
    VView in[4];
    int n = 0;
    VView tri[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        VView p = tri[i], q = tri[(i + 1) % 3];
        int pin = p.z <= zlim, qin = q.z <= zlim;
        if (pin)
            in[n++] = p;
        if (pin != qin) {
            float t = (zlim - p.z) / (q.z - p.z);
            in[n++] = lerp3(p, q, t);
        }
    }
    if (n < 3)
        return;
    ClipVert cv[8];
    for (int i = 0; i < n; i++)
        project(in[i], &cv[i]);
    for (int i = 1; i + 1 < n; i++)
        raster_tri(cv[0], cv[i], cv[i + 1], &s_tex, s_tex_on, s_tex_func);
}

void soft_r_draw(RPrim prim, const RVertex *v, int count)
{
    if (!v || count <= 0)
        return;
    if (!s_two_d) {
        if (prim == R_PRIM_LINES) {
            for (int i = 0; i + 1 < count; i += 2) {
                VView a, b;
                to_view(&v[i], &a);
                to_view(&v[i + 1], &b);
                // Clip each line segment against the near plane.
                float zlim = -(s_near_set ? s_near : 0.05f);
                if (a.z > zlim && b.z > zlim) continue;
                if (a.z > zlim) { float t = (zlim - a.z) / (b.z - a.z); a = lerp3(a, b, t); }
                else if (b.z > zlim) { float t = (zlim - b.z) / (a.z - b.z); b = lerp3(b, a, t); }
                ClipVert ca, cb;
                project(a, &ca); project(b, &cb);
                draw_line(ca, cb);
            }
            return;
        }
        if (prim == R_PRIM_TRIANGLES) {
            for (int i = 0; i + 2 < count; i += 3) {
                VView a, b, c;
                to_view(&v[i], &a); to_view(&v[i + 1], &b); to_view(&v[i + 2], &c);
                clip_and_draw(a, b, c);
            }
            return;
        }
        if (prim == R_PRIM_TRIANGLE_STRIP) {
            for (int i = 0; i + 2 < count; i++) {
                VView a, b, c;
                to_view(&v[i], &a); to_view(&v[i + 1], &b); to_view(&v[i + 2], &c);
                if (i & 1) clip_and_draw(a, c, b);
                else clip_and_draw(a, b, c);
            }
            return;
        }
        return;
    }

    // 2D: pixel positions, no matrices, no fog/depth.
    if (prim == R_PRIM_LINES) {
        for (int i = 0; i + 1 < count; i += 2) {
            ClipVert a = {v[i].x, v[i].y, 0, 1, v[i].u, v[i].v,
                          (float)(v[i].color & 0xFF), (float)((v[i].color >> 8) & 0xFF),
                          (float)((v[i].color >> 16) & 0xFF), (float)((v[i].color >> 24) & 0xFF), 1, 0};
            ClipVert b = {v[i + 1].x, v[i + 1].y, 0, 1, v[i + 1].u, v[i + 1].v,
                          (float)(v[i + 1].color & 0xFF), (float)((v[i + 1].color >> 8) & 0xFF),
                          (float)((v[i + 1].color >> 16) & 0xFF), (float)((v[i + 1].color >> 24) & 0xFF), 1, 0};
            draw_line(a, b);
        }
        return;
    }
    for (int i = 0; i + 1 < count; i += 2) {
        const RVertex *p0 = &v[i], *p1 = &v[i + 1];
        ClipVert q[4];
        q[0] = (ClipVert){p0->x, p0->y, 0, 1, p0->u, p0->v, (float)(p0->color & 0xFF),
                          (float)((p0->color >> 8) & 0xFF), (float)((p0->color >> 16) & 0xFF),
                          (float)((p0->color >> 24) & 0xFF), 1, 0};
        q[1] = (ClipVert){p1->x, p0->y, 0, 1, p1->u, p0->v, (float)(p0->color & 0xFF),
                          (float)((p0->color >> 8) & 0xFF), (float)((p0->color >> 16) & 0xFF),
                          (float)((p0->color >> 24) & 0xFF), 1, 0};
        q[2] = (ClipVert){p1->x, p1->y, 0, 1, p1->u, p1->v, (float)(p1->color & 0xFF),
                          (float)((p1->color >> 8) & 0xFF), (float)((p1->color >> 16) & 0xFF),
                          (float)((p1->color >> 24) & 0xFF), 1, 0};
        q[3] = (ClipVert){p0->x, p1->y, 0, 1, p0->u, p1->v, (float)(p1->color & 0xFF),
                          (float)((p1->color >> 8) & 0xFF), (float)((p1->color >> 16) & 0xFF),
                          (float)((p1->color >> 24) & 0xFF), 1, 0};
        raster_tri(q[0], q[1], q[2], &s_tex, s_tex_on, s_tex_func);
        raster_tri(q[0], q[2], q[3], &s_tex, s_tex_on, s_tex_func);
    }
}

void soft_r_draw_native(RPrim prim, const RVertexNative *v, int count)
{
    if (!v || count <= 0 || s_two_d)
        return;
    if (prim == R_PRIM_TRIANGLES) {
        for (int i = 0; i + 2 < count; i += 3) {
            VView a, b, c;
            to_view_raw(v[i].x, v[i].y, v[i].z, v[i].u, v[i].v, v[i].color, &a);
            to_view_raw(v[i + 1].x, v[i + 1].y, v[i + 1].z, v[i + 1].u, v[i + 1].v, v[i + 1].color, &b);
            to_view_raw(v[i + 2].x, v[i + 2].y, v[i + 2].z, v[i + 2].u, v[i + 2].v, v[i + 2].color, &c);
            clip_and_draw(a, b, c);
        }
    } else if (prim == R_PRIM_TRIANGLE_STRIP) {
        for (int i = 0; i + 2 < count; i++) {
            VView a, b, c;
            to_view_raw(v[i].x, v[i].y, v[i].z, v[i].u, v[i].v, v[i].color, &a);
            to_view_raw(v[i + 1].x, v[i + 1].y, v[i + 1].z, v[i + 1].u, v[i + 1].v, v[i + 1].color, &b);
            to_view_raw(v[i + 2].x, v[i + 2].y, v[i + 2].z, v[i + 2].u, v[i + 2].v, v[i + 2].color, &c);
            if (i & 1) clip_and_draw(a, c, b);
            else clip_and_draw(a, b, c);
        }
    }
}

void soft_r_copy_frame_rgb(unsigned char *dst)
{
    if (!dst) return;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            uint16_t c = FB(y)[x];
            unsigned int r = c & 0x1F, g = (c >> 5) & 0x3F, b = c >> 11;
            unsigned char *o = dst + ((size_t)y * SW + x) * 3;
            o[0] = (unsigned char)(r << 3 | r >> 2);
            o[1] = (unsigned char)(g << 2 | g >> 4);
            o[2] = (unsigned char)(b << 3 | b >> 2);
        }
}

void soft_r_copy_frame(uint16_t *dst, int stride)
{
    if (!dst) return;
    for (int y = 0; y < SH; y++)
        memcpy(dst + (size_t)y * stride, FB(y), (size_t)SW * sizeof(uint16_t));
}

void soft_r_set_near(float near_m) { s_near = near_m; s_near_set = 1; }