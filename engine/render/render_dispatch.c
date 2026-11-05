// Runtime dispatch between the software and OpenGL renderers. Both backends
// implement the same render_api.h functions under a private prefix; this file
// exposes the public r_* names and picks one.
#include "render_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int soft_r_init(const char *);
void soft_r_shutdown(void);
const char *soft_r_backend_name(void);
void soft_r_set_headless(int);
void soft_r_size(int *, int *);
void soft_r_set_near(float);
void soft_r_begin_3d(void);
void soft_r_begin_2d(void);
void soft_r_inset_3d(int on);
void soft_r_end_frame(void);
void soft_r_present(void);
void soft_r_clear(uint32_t, int);
void soft_r_clear_depth(int);
void soft_r_viewport(int, int, int, int);
void soft_r_scissor(int, int, int, int);
void soft_r_set_fog(int);
void soft_r_set_fog_params(float, float, uint32_t);
void soft_r_set_depth_test(int);
void soft_r_set_depth_write(int);
void soft_r_set_cull(int);
void soft_r_set_alpha_test(int);
void soft_r_set_blend(RBlend);
void soft_r_set_tex_func(RTexFunc);
void soft_r_set_tex_wrap(RWrap);
void soft_r_set_uv_scale(float, float);
void soft_r_set_uv_offset(float, float);
void soft_r_bind_texture(const RTexDesc *);
void soft_r_matrix_mode(RMatrixMode);
void soft_r_load_identity(void);
void soft_r_load_matrix(const RMat4 *);
void soft_r_store_matrix(RMat4 *);
void soft_r_translate(const RVec3 *);
void soft_r_rotate_x(float);
void soft_r_rotate_y(float);
void soft_r_rotate_z(float);
void soft_r_scale(const RVec3 *);
void soft_r_perspective(float, float, float, float);
void soft_r_look_at(const RVec3 *, const RVec3 *, const RVec3 *);
void soft_r_update_matrix(void);
void soft_r_draw(RPrim, const RVertex *, int);
void soft_r_draw_native(RPrim, const RVertexNative *, int);
void *soft_r_temp_alloc(int);
void soft_r_temp_reset(void);
void soft_r_copy_frame(uint16_t *, int);
int soft_r_set_frame_size(int, int);
void soft_r_copy_frame_rgb(unsigned char *);
void soft_r_set_native(int);

int gl_r_init(const char *);
void gl_r_shutdown(void);
const char *gl_r_backend_name(void);
void gl_r_set_headless(int);
void gl_r_size(int *, int *);
void gl_r_set_near(float);
void gl_r_begin_3d(void);
void gl_r_begin_2d(void);
void gl_r_inset_3d(int on);
void gl_r_end_frame(void);
void gl_r_present(void);
void gl_r_clear(uint32_t, int);
void gl_r_clear_depth(int);
void gl_r_viewport(int, int, int, int);
void gl_r_scissor(int, int, int, int);
void gl_r_set_fog(int);
void gl_r_set_fog_params(float, float, uint32_t);
void gl_r_set_depth_test(int);
void gl_r_set_depth_write(int);
void gl_r_set_cull(int);
void gl_r_set_alpha_test(int);
void gl_r_set_blend(RBlend);
void gl_r_set_tex_func(RTexFunc);
void gl_r_set_tex_wrap(RWrap);
void gl_r_set_uv_scale(float, float);
void gl_r_set_uv_offset(float, float);
void gl_r_bind_texture(const RTexDesc *);
void gl_r_matrix_mode(RMatrixMode);
void gl_r_load_identity(void);
void gl_r_load_matrix(const RMat4 *);
void gl_r_store_matrix(RMat4 *);
void gl_r_translate(const RVec3 *);
void gl_r_rotate_x(float);
void gl_r_rotate_y(float);
void gl_r_rotate_z(float);
void gl_r_scale(const RVec3 *);
void gl_r_perspective(float, float, float, float);
void gl_r_look_at(const RVec3 *, const RVec3 *, const RVec3 *);
void gl_r_update_matrix(void);
void gl_r_draw(RPrim, const RVertex *, int);
void gl_r_draw_native(RPrim, const RVertexNative *, int);
void *gl_r_temp_alloc(int);
void gl_r_temp_reset(void);
void gl_r_copy_frame(uint16_t *, int);
int gl_r_set_frame_size(int, int);
void gl_r_copy_frame_rgb(unsigned char *);
void gl_r_set_native(int);

static int s_gl;
static int s_headless;
static int s_fw, s_fh, s_native;   // requested frame size (0 = backend default) and look
static int s_ui = 1;               // 2D coordinates are UI points of s_ui pixels
static int s_2d, s_inset;
static RVertex *s_scaled;
static int s_scaled_cap;

int r_set_frame_size(int w, int h)
{
    s_fw = w;
    s_fh = h;
    return s_gl ? gl_r_set_frame_size(w, h) : soft_r_set_frame_size(w, h);
}

void r_set_native(int on)
{
    s_native = on;
    if (s_gl) gl_r_set_native(on);
    else soft_r_set_native(on);
}

int r_native(void) { return s_native; }
void r_set_ui_scale(int scale) { s_ui = scale < 1 ? 1 : (scale > 8 ? 8 : scale); }
int r_ui_scale(void) { return s_ui; }

int r_init(const char *backend)
{
    s_gl = backend && (!strcmp(backend, "gl") || !strcmp(backend, "opengl"));
    if (s_fw > 0 && s_fh > 0) {
        if (s_gl) gl_r_set_frame_size(s_fw, s_fh);
        else soft_r_set_frame_size(s_fw, s_fh);
    }
    if (s_gl) gl_r_set_native(s_native);
    else soft_r_set_native(s_native);
    if (s_gl) {
        gl_r_set_headless(s_headless);
        if (gl_r_init(backend) != 0) {
            s_gl = 0;
            return -1;
        }
        return 0;
    }
    soft_r_set_headless(s_headless);
    return soft_r_init(backend);
}

void r_shutdown(void)
{
    if (s_gl) gl_r_shutdown();
    else soft_r_shutdown();
}
const char *r_backend_name(void) { return s_gl ? gl_r_backend_name() : soft_r_backend_name(); }
void r_set_headless(int on)
{
    s_headless = on;
    if (s_gl) gl_r_set_headless(on);
    else soft_r_set_headless(on);
}
void r_size(int *w, int *h) { if (s_gl) gl_r_size(w, h); else soft_r_size(w, h); }
void r_set_near(float n) { if (s_gl) gl_r_set_near(n); else soft_r_set_near(n); }
void r_ui_size(int *w, int *h)
{
    int pw = 0, ph = 0;
    r_size(&pw, &ph);
    if (w) *w = pw / s_ui;
    if (h) *h = ph / s_ui;
}
void r_begin_3d(void) { s_2d = 0; s_inset = 0; if (s_gl) gl_r_begin_3d(); else soft_r_begin_3d(); }
void r_begin_2d(void) { s_2d = 1; s_inset = 0; if (s_gl) gl_r_begin_2d(); else soft_r_begin_2d(); }
void r_inset_3d(int on) { s_inset = on; if (s_gl) gl_r_inset_3d(on); else soft_r_inset_3d(on); }
void r_end_frame(void) { if (s_gl) gl_r_end_frame(); else soft_r_end_frame(); }
void r_present(void) { if (s_gl) gl_r_present(); else soft_r_present(); }
void r_clear(uint32_t c, int d) { if (s_gl) gl_r_clear(c, d); else soft_r_clear(c, d); }
void r_clear_depth(int d) { if (s_gl) gl_r_clear_depth(d); else soft_r_clear_depth(d); }
// With a UI scale, viewports and scissors are given in UI points too.
void r_viewport(int x, int y, int w, int h)
{
    int k = s_ui;
    if (s_gl) gl_r_viewport(x * k, y * k, w * k, h * k);
    else soft_r_viewport(x * k, y * k, w * k, h * k);
}
void r_scissor(int x, int y, int w, int h)
{
    int k = s_ui;
    if (s_gl) gl_r_scissor(x * k, y * k, w * k, h * k);
    else soft_r_scissor(x * k, y * k, w * k, h * k);
}
void r_set_fog(int on) { if (s_gl) gl_r_set_fog(on); else soft_r_set_fog(on); }
void r_set_fog_params(float n, float f, uint32_t c) { if (s_gl) gl_r_set_fog_params(n, f, c); else soft_r_set_fog_params(n, f, c); }
void r_set_depth_test(int on) { if (s_gl) gl_r_set_depth_test(on); else soft_r_set_depth_test(on); }
void r_set_depth_write(int on) { if (s_gl) gl_r_set_depth_write(on); else soft_r_set_depth_write(on); }
void r_set_cull(int on) { if (s_gl) gl_r_set_cull(on); else soft_r_set_cull(on); }
void r_set_alpha_test(int on) { if (s_gl) gl_r_set_alpha_test(on); else soft_r_set_alpha_test(on); }
void r_set_blend(RBlend b) { if (s_gl) gl_r_set_blend(b); else soft_r_set_blend(b); }
void r_set_tex_func(RTexFunc f) { if (s_gl) gl_r_set_tex_func(f); else soft_r_set_tex_func(f); }
void r_set_tex_wrap(RWrap w) { if (s_gl) gl_r_set_tex_wrap(w); else soft_r_set_tex_wrap(w); }
void r_set_uv_scale(float u, float v) { if (s_gl) gl_r_set_uv_scale(u, v); else soft_r_set_uv_scale(u, v); }
void r_set_uv_offset(float u, float v) { if (s_gl) gl_r_set_uv_offset(u, v); else soft_r_set_uv_offset(u, v); }
void r_bind_texture(const RTexDesc *t) { if (s_gl) gl_r_bind_texture(t); else soft_r_bind_texture(t); }
void r_matrix_mode(RMatrixMode m) { if (s_gl) gl_r_matrix_mode(m); else soft_r_matrix_mode(m); }
void r_load_identity(void) { if (s_gl) gl_r_load_identity(); else soft_r_load_identity(); }
void r_load_matrix(const RMat4 *m) { if (s_gl) gl_r_load_matrix(m); else soft_r_load_matrix(m); }
void r_store_matrix(RMat4 *m) { if (s_gl) gl_r_store_matrix(m); else soft_r_store_matrix(m); }
void r_translate(const RVec3 *v) { if (s_gl) gl_r_translate(v); else soft_r_translate(v); }
void r_rotate_x(float a) { if (s_gl) gl_r_rotate_x(a); else soft_r_rotate_x(a); }
void r_rotate_y(float a) { if (s_gl) gl_r_rotate_y(a); else soft_r_rotate_y(a); }
void r_rotate_z(float a) { if (s_gl) gl_r_rotate_z(a); else soft_r_rotate_z(a); }
void r_scale(const RVec3 *v) { if (s_gl) gl_r_scale(v); else soft_r_scale(v); }
void r_perspective(float f, float a, float n, float r) { if (s_gl) gl_r_perspective(f, a, n, r); else soft_r_perspective(f, a, n, r); }
void r_look_at(const RVec3 *e, const RVec3 *a, const RVec3 *u) { if (s_gl) gl_r_look_at(e, a, u); else soft_r_look_at(e, a, u); }
void r_update_matrix(void) { if (s_gl) gl_r_update_matrix(); else soft_r_update_matrix(); }
static RVertex *scaled_buf(int n)
{
    if (n > s_scaled_cap) {
        int cap = s_scaled_cap ? s_scaled_cap : 1024;
        while (cap < n) cap *= 2;
        RVertex *b = (RVertex *)realloc(s_scaled, sizeof(RVertex) * (size_t)cap);
        if (!b) return NULL;
        s_scaled = b;
        s_scaled_cap = cap;
    }
    return s_scaled;
}

static void draw_raw(RPrim p, const RVertex *v, int n) { if (s_gl) gl_r_draw(p, v, n); else soft_r_draw(p, v, n); }

void r_draw(RPrim p, const RVertex *v, int n)
{
    if (s_ui == 1 || !s_2d || s_inset || n <= 0) { draw_raw(p, v, n); return; }
    float k = (float)s_ui;
    if (p == R_PRIM_LINES) {
        // A UI line is one point wide: a quad of s_ui pixels.
        int segs = n / 2;
        RVertex *b = scaled_buf(segs * 6);
        if (!b) return;
        for (int i = 0; i < segs; i++) {
            const RVertex *a = &v[i * 2], *c = &v[i * 2 + 1];
            float ax = a->x * k, ay = a->y * k, cx = c->x * k, cy = c->y * k;
            float dx = cx - ax, dy = cy - ay, l = sqrtf(dx * dx + dy * dy);
            float nx = l > 0 ? -dy / l * k * 0.5f : 0, ny = l > 0 ? dx / l * k * 0.5f : k * 0.5f;
            if (l > 0) { ax -= dx / l * k * 0.5f; ay -= dy / l * k * 0.5f; cx += dx / l * k * 0.5f; cy += dy / l * k * 0.5f; }
            RVertex q[4] = {*a, *a, *c, *c};
            q[0].x = ax + nx; q[0].y = ay + ny;
            q[1].x = ax - nx; q[1].y = ay - ny;
            q[2].x = cx - nx; q[2].y = cy - ny;
            q[3].x = cx + nx; q[3].y = cy + ny;
            b[i * 6 + 0] = q[0]; b[i * 6 + 1] = q[1]; b[i * 6 + 2] = q[2];
            b[i * 6 + 3] = q[0]; b[i * 6 + 4] = q[2]; b[i * 6 + 5] = q[3];
        }
        draw_raw(R_PRIM_TRIANGLES, b, segs * 6);
        return;
    }
    RVertex *b = scaled_buf(n);
    if (!b) return;
    for (int i = 0; i < n; i++) {
        b[i] = v[i];
        b[i].x = v[i].x * k;
        b[i].y = v[i].y * k;
    }
    draw_raw(p, b, n);
}
void r_draw_native(RPrim p, const RVertexNative *v, int n) { if (s_gl) gl_r_draw_native(p, v, n); else soft_r_draw_native(p, v, n); }
void *r_temp_alloc(int n) { return s_gl ? gl_r_temp_alloc(n) : soft_r_temp_alloc(n); }
void r_temp_reset(void) { if (s_gl) gl_r_temp_reset(); else soft_r_temp_reset(); }
void r_copy_frame(uint16_t *d, int s) { if (s_gl) gl_r_copy_frame(d, s); else soft_r_copy_frame(d, s); }
void r_copy_frame_rgb(unsigned char *d) { if (s_gl) gl_r_copy_frame_rgb(d); else soft_r_copy_frame_rgb(d); }
