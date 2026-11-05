// The small rendering interface the game and world code draw through.
//
// It describes intent (begin a 3D pass, bind a paletted texture, draw triangles,
// blend additively) instead of mirroring OpenGL. Backends:
//   engine/render/soft/  CPU rasteriser, 16-bit colour + depth (works everywhere, headless)
//   engine/render/gl/    OpenGL 3.3
//
// No SDL or OpenGL type may appear here.
#ifndef MC_RENDER_RENDER_API_H
#define MC_RENDER_RENDER_API_H

#include <stdint.h>

#include "engine/core/math.h"

typedef MxMat4 RMat4;
typedef struct { float x, y, z; } RVec3;

// Position, normalised texture coordinates and ABGR8888 vertex colour.
// 2D draws use pixel positions and UV in [0,1]; 3D draws use model-space positions.
typedef struct {
    float x, y, z;
    float u, v;
    uint32_t color;
} RVertex;

// Native asset vertex layout (MCM/world expansion): UV, colour, position.
// Backends that can consume it directly avoid a per-frame conversion.
typedef struct {
    float u, v;
    uint32_t color;
    float x, y, z;
} RVertexNative;

typedef enum {
    R_PRIM_TRIANGLES = 0,
    R_PRIM_TRIANGLE_STRIP,
    R_PRIM_SPRITES,       // each pair of vertices is an axis-aligned rectangle
    R_PRIM_LINES,
} RPrim;

typedef enum { R_MATRIX_PROJECTION = 0, R_MATRIX_VIEW = 1, R_MATRIX_MODEL = 2 } RMatrixMode;

typedef enum { R_BLEND_NONE = 0, R_BLEND_ALPHA, R_BLEND_ADD } RBlend;

typedef enum { R_TEXFUNC_MODULATE = 0, R_TEXFUNC_REPLACE } RTexFunc;

typedef enum { R_WRAP_REPEAT = 0, R_WRAP_CLAMP } RWrap;

typedef enum { R_TEX_T4 = 0, R_TEX_565 } RTexFormat;

typedef struct {
    int w, h;
    RTexFormat format;
    int alpha0;                 // T4: palette entry 0 is transparent
    const void *pixels;         // T4: packed nibbles (low nibble left); 565: uint16_t
    const uint32_t *clut;       // T4: 16 ABGR entries
    RWrap wrap;
    int dynamic;                // pixels change this frame: upload every bind
} RTexDesc;

// Selects the renderer. "software", "gl" or "auto". Returns 0 on success; when GL
// fails, auto falls back to software and 0 is still returned.
int r_init(const char *backend);
// Headless runs skip window/video present calls (used by --headless).
void r_set_headless(int on);
// Near plane used to clip triangles before the perspective divide.
void r_set_near(float near_m);
void r_shutdown(void);
const char *r_backend_name(void);

// Per frame. begin_3d starts a scene at the internal resolution; begin_2d switches
// to the UI pass; end_frame closes the frame. present() shows it (and paces).
void r_begin_3d(void);
void r_begin_2d(void);
// Inside the 2D pass: 1 = draw 3D (with the current matrices) for an inset preview, 0 = back.
void r_inset_3d(int on);
void r_end_frame(void);
void r_present(void);

// Abgr8888 clear colour. clear_depth: -1 = leave depth, otherwise write that value
// (reversed depth: near = 0xFFFF, far = 0).
void r_clear(uint32_t abgr, int clear_depth);
void r_clear_depth(int value);
void r_viewport(int x, int y, int w, int h);
void r_scissor(int x, int y, int w, int h);

void r_set_fog(int enabled);
void r_set_fog_params(float near_m, float far_m, uint32_t abgr);
void r_set_depth_test(int enabled);
void r_set_depth_write(int enabled);   // 1 = write depth, 0 = test only
void r_set_cull(int enabled);          // 1 = cull back faces
void r_set_alpha_test(int enabled);    // discard alpha == 0
void r_set_blend(RBlend blend);
void r_set_tex_func(RTexFunc func);
void r_set_tex_wrap(RWrap wrap);
void r_set_uv_scale(float u, float v);
void r_set_uv_offset(float u, float v);
void r_bind_texture(const RTexDesc *tex);   // NULL = untextured

// Matrix state. Angles are radians; perspective matches sceGumPerspective.
void r_matrix_mode(RMatrixMode mode);
void r_load_identity(void);
void r_load_matrix(const RMat4 *m);
void r_store_matrix(RMat4 *m);
void r_translate(const RVec3 *v);
void r_rotate_x(float radians);
void r_rotate_y(float radians);
void r_rotate_z(float radians);
void r_scale(const RVec3 *v);
void r_perspective(float fov_deg, float aspect, float near_m, float far_m);
void r_look_at(const RVec3 *eye, const RVec3 *at, const RVec3 *up);
void r_update_matrix(void);            // flush pending matrix state

// Draws with the current state. The vertex data is consumed immediately.
void r_draw(RPrim prim, const RVertex *v, int count);
void r_draw_native(RPrim prim, const RVertexNative *v, int count);

// Per-frame scratch memory for temporary vertex arrays (replaces sceGuGetMemory).
// Valid until the next r_begin_3d().
void *r_temp_alloc(int bytes);
void r_temp_reset(void);

// Copies the finished frame as RGB565 (stride pixels per line) or as 8-bit RGB
// (w * h * 3 bytes, full colour in the native look; see r_size).
void r_copy_frame(uint16_t *dst, int stride);
void r_copy_frame_rgb(unsigned char *dst);

// Current frame size in pixels (480x272 for a retro game unless it asks otherwise).
void r_size(int *w, int *h);

// Frame size in pixels; call before r_init or at any time after (targets are rebuilt).
int r_set_frame_size(int w, int h);
// Look: 0 = retro (RGB565 with ordered dither), 1 = native (RGBA8 on GL,
// no dither). Desktop tools and high-resolution games use native.
void r_set_native(int on);
int r_native(void);
// UI scale for high-DPI screens: in the 2D pass, positions, viewports and scissors are
// in UI points of `scale` pixels (lines become `scale` pixels wide). 1 = off.
void r_set_ui_scale(int scale);
int r_ui_scale(void);
void r_ui_size(int *w, int *h);   // frame size in UI points

#endif