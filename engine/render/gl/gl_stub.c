// Fallback symbols when the OpenGL backend is disabled at configure time.
#include "../render_api.h"

#ifdef MC_NO_OPENGL

int gl_r_init(const char *b) { (void)b; return -1; }
void gl_r_shutdown(void) {}
const char *gl_r_backend_name(void) { return "opengl"; }
void gl_r_set_headless(int on) { (void)on; }
void gl_r_size(int *w, int *h) { if (w) *w = 480; if (h) *h = 272; }
int gl_r_set_frame_size(int w, int h) { (void)w; (void)h; return -1; }
void gl_r_set_native(int on) { (void)on; }
void gl_r_copy_frame_rgb(unsigned char *d) { (void)d; }
void gl_r_set_near(float n) { (void)n; }
void gl_r_begin_3d(void) {}
void gl_r_begin_2d(void) {}
void gl_r_inset_3d(int on) { (void)on; }
void gl_r_end_frame(void) {}
void gl_r_present(void) {}
void gl_r_clear(uint32_t c, int d) { (void)c; (void)d; }
void gl_r_clear_depth(int d) { (void)d; }
void gl_r_viewport(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void gl_r_scissor(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void gl_r_set_fog(int on) { (void)on; }
void gl_r_set_fog_params(float n, float f, uint32_t c) { (void)n; (void)f; (void)c; }
void gl_r_set_depth_test(int on) { (void)on; }
void gl_r_set_depth_write(int on) { (void)on; }
void gl_r_set_cull(int on) { (void)on; }
void gl_r_set_alpha_test(int on) { (void)on; }
void gl_r_set_blend(RBlend b) { (void)b; }
void gl_r_set_tex_func(RTexFunc f) { (void)f; }
void gl_r_set_tex_wrap(RWrap w) { (void)w; }
void gl_r_set_uv_scale(float u, float v) { (void)u; (void)v; }
void gl_r_set_uv_offset(float u, float v) { (void)u; (void)v; }
void gl_r_bind_texture(const RTexDesc *t) { (void)t; }
void gl_r_matrix_mode(RMatrixMode m) { (void)m; }
void gl_r_load_identity(void) {}
void gl_r_load_matrix(const RMat4 *m) { (void)m; }
void gl_r_store_matrix(RMat4 *m) { (void)m; }
void gl_r_translate(const RVec3 *v) { (void)v; }
void gl_r_rotate_x(float a) { (void)a; }
void gl_r_rotate_y(float a) { (void)a; }
void gl_r_rotate_z(float a) { (void)a; }
void gl_r_scale(const RVec3 *v) { (void)v; }
void gl_r_perspective(float f, float a, float n, float r) { (void)f; (void)a; (void)n; (void)r; }
void gl_r_look_at(const RVec3 *e, const RVec3 *a, const RVec3 *u) { (void)e; (void)a; (void)u; }
void gl_r_update_matrix(void) {}
void gl_r_draw(RPrim p, const RVertex *v, int n) { (void)p; (void)v; (void)n; }
void gl_r_draw_native(RPrim p, const RVertexNative *v, int n) { (void)p; (void)v; (void)n; }
void *gl_r_temp_alloc(int n) { (void)n; return 0; }
void gl_r_temp_reset(void) {}
void gl_r_copy_frame(uint16_t *d, int s) { (void)d; (void)s; }

#endif
