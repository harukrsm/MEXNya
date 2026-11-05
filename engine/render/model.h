// .mcm models and .mct textures written by tools/mcm.py.
#ifndef MC_RENDER_MODEL_H
#define MC_RENDER_MODEL_H

#include "render_api.h"

// Batch flags (match tools/mcm.py)
#define MCM_BATCH_NOFOG    1
#define MCM_BATCH_ADDITIVE 2
#define MCM_BATCH_ALPHA    4
#define MCM_BATCH_DOUBLE   8
#define MCM_BATCH_STRIP    32  // world chunks: one triangle strip

#define MCM_TEX_ALPHA0     1

typedef struct {
    float u, v;
    unsigned int color;
    float x, y, z;
} McmVertex;

typedef struct {
    unsigned short w, h;
    unsigned int data_offset;  // 4-bit texels, 16-byte aligned
    unsigned int clut_offset;  // 16 x ABGR, 16-byte aligned (the format requires it)
    unsigned int flags;
} McmTexture;

typedef struct {
    char name[16];
    float pivot[3];
    int parent;
    unsigned int first_batch, batch_count;
} McmPart;

typedef struct {
    int tex;
    unsigned int first_vertex, vertex_count, flags;
} McmBatch;

typedef struct {
    char magic[4];
    unsigned int tex_count, part_count, batch_count, vert_count;
    unsigned int tex_offset, part_offset, batch_offset, vert_offset;
    float bounds_min[3], bounds_max[3];
    unsigned int reserved;
} McmHeader;

typedef struct {
    McmHeader *file;  // the whole file, 64-byte aligned
    McmTexture *textures;
    McmPart *parts;
    McmBatch *batches;
    McmVertex *verts;
    RVertexNative *rverts;  // expanded once at load; 3D backends draw this
    int size;
} Model;

// Loads a model (or a .mct texture, which is a model with one texture and no parts).
int model_load(Model *m, const char *rel_path);
void model_free(Model *m);

// Index of a part by name, or -1.
int model_find_part(const Model *m, const char *name);

// Draws every part with the current GUM model matrix.
void model_draw(const Model *m);

// Draws one part with the current GUM model matrix (caller sets up the transform).
void model_draw_part(const Model *m, int part);

// Draws all parts posed: each part is rotated about its pivot (yaw, then pitch,
// then roll: rot[i] = {pitch, yaw, roll}) on top of its parent; root parts sit
// on `root`. Part matrices are written to `out` (n entries) if not NULL.
void model_draw_posed(const Model *m, const RMat4 *root, const float (*rot)[3], int n,
                      RMat4 *out);

// While set, batches using texture 0 draw with texture 0 of `tex_model` instead (NULL = off).
void model_set_tex_override(const Model *tex_model);

// Draws a model turning slowly inside a screen rectangle (item previews): sets its own
// projection/viewport/depth state and restores the screen viewport afterwards.
void model_preview(int x, int y, int w, int h, const Model *m, float yaw_rad);

// Binds a texture from a loaded model/texture file. Skips redundant binds.
void tex_bind(const Model *m, int tex_index);

// Uploads a raw RGB565 image (album thumbnails) as the current 2D texture.
void tex_bind_565(const unsigned short *pixels, int w, int h);
void tex_bind_t4_raw(const void *pixels, int w, int h, const uint32_t *clut, int alpha0);

// Forget the cached binding (call after anything else touched texture state).
void tex_invalidate(void);

#endif