#include "model.h"
#include "gfx.h"
#include "stats.h"
#include "engine/platform/file.h"
#include "engine/platform/platform.h"

#include <stdlib.h>
#include <string.h>

RenderStats g_rstats;

static const void *s_bound_tex;  // texture data pointer currently bound, or NULL
static RTexDesc s_texdesc;

// Every table, texture and batch inside the file (a cut-short file used to pass the offset
// checks and send the renderer reading past the end of the buffer).
static int fits(const McmHeader *h, int size)
{
    unsigned long long n = (unsigned long long)size;
#define TABLE(off, count, type) ((unsigned long long)(off) + (unsigned long long)(count) * sizeof(type) <= n)
    if (!TABLE(h->tex_offset, h->tex_count, McmTexture) || !TABLE(h->part_offset, h->part_count, McmPart) ||
        !TABLE(h->batch_offset, h->batch_count, McmBatch) || !TABLE(h->vert_offset, h->vert_count, McmVertex))
        return 0;
#undef TABLE
    const unsigned char *b = (const unsigned char *)h;
    const McmTexture *t = (const McmTexture *)(b + h->tex_offset);
    for (unsigned int i = 0; i < h->tex_count; i++)
        if ((unsigned long long)t[i].data_offset + (unsigned long long)t[i].w * t[i].h / 2 > n ||
            (unsigned long long)t[i].clut_offset + 64 > n)
            return 0;
    const McmBatch *bt = (const McmBatch *)(b + h->batch_offset);
    for (unsigned int i = 0; i < h->batch_count; i++)
        if ((bt[i].tex >= 0 && (unsigned int)bt[i].tex >= h->tex_count) ||
            (unsigned long long)bt[i].first_vertex + bt[i].vertex_count > h->vert_count)
            return 0;
    const McmPart *pt = (const McmPart *)(b + h->part_offset);
    for (unsigned int i = 0; i < h->part_count; i++)
        if ((unsigned long long)pt[i].first_batch + pt[i].batch_count > h->batch_count)
            return 0;
    return 1;
}

static int expand_model(Model *m)
{
    m->rverts = malloc((size_t)m->file->vert_count * sizeof(RVertexNative));
    if (!m->rverts)
        return -1;
    for (unsigned int i = 0; i < m->file->vert_count; i++) {
        const McmVertex *v = &m->verts[i];
        m->rverts[i].x = v->x;
        m->rverts[i].y = v->y;
        m->rverts[i].z = v->z;
        m->rverts[i].u = v->u;
        m->rverts[i].v = v->v;
        m->rverts[i].color = v->color;
    }
    return 0;
}

int model_load(Model *m, const char *rel_path)
{
    memset(m, 0, sizeof(*m));
    int size = 0;
    unsigned char *buf = file_load(rel_path, &size);
    if (!buf)
        return -1;
    McmHeader *h = (McmHeader *)buf;
    if (size < (int)sizeof(McmHeader) || (memcmp(h->magic, "MCM1", 4) && memcmp(h->magic, "MCT1", 4)) ||
        !fits(h, size)) {
        free(buf);
        return -2;
    }
    m->file = h;
    m->size = size;
    m->textures = (McmTexture *)(buf + h->tex_offset);
    m->parts = (McmPart *)(buf + h->part_offset);
    m->batches = (McmBatch *)(buf + h->batch_offset);
    m->verts = (McmVertex *)(buf + h->vert_offset);
    if (h->vert_count && expand_model(m) != 0) {
        free(buf);
        memset(m, 0, sizeof(*m));
        return -3;
    }
    plat_flush_range(buf, (size_t)size);  // keep the CPU and GPU views in sync where that matters
    return 0;
}

void model_free(Model *m)
{
    free(m->rverts);
    free(m->file);
    memset(m, 0, sizeof(*m));
}

int model_find_part(const Model *m, const char *name)
{
    for (unsigned int i = 0; i < m->file->part_count; i++)
        if (strncmp(m->parts[i].name, name, sizeof(m->parts[i].name)) == 0)
            return (int)i;
    return -1;
}

void tex_invalidate(void) { s_bound_tex = NULL; }

void tex_bind(const Model *m, int tex_index)
{
    if (tex_index < 0) {
        r_bind_texture(NULL);
        s_bound_tex = NULL;
        return;
    }
    const McmTexture *t = &m->textures[tex_index];
    const void *data = (const unsigned char *)m->file + t->data_offset;
    if (data == s_bound_tex)
        return;
    s_bound_tex = data;
    g_rstats.texture_binds++;
    s_texdesc.w = t->w;
    s_texdesc.h = t->h;
    s_texdesc.format = R_TEX_T4;
    s_texdesc.alpha0 = (t->flags & MCM_TEX_ALPHA0) != 0;
    s_texdesc.pixels = data;
    s_texdesc.clut = (const uint32_t *)((const unsigned char *)m->file + t->clut_offset);
    s_texdesc.wrap = R_WRAP_REPEAT;
    s_texdesc.dynamic = 0;
    r_bind_texture(&s_texdesc);
    r_set_tex_func(R_TEXFUNC_MODULATE);
    r_set_uv_scale(1.0f, 1.0f);
    r_set_uv_offset(0.0f, 0.0f);
}

// Texture 2D uploads (album thumbnails) bypass model files.
void tex_bind_565(const unsigned short *pixels, int w, int h)
{
    s_texdesc.w = w;
    s_texdesc.h = h;
    s_texdesc.format = R_TEX_565;
    s_texdesc.alpha0 = 0;
    s_texdesc.pixels = pixels;
    s_texdesc.clut = NULL;
    s_texdesc.wrap = R_WRAP_CLAMP;
    s_texdesc.dynamic = 1;
    r_bind_texture(&s_texdesc);
    r_set_tex_func(R_TEXFUNC_REPLACE);
    s_bound_tex = NULL;
}

// Raw 4-bit texture (the TV snow changes its palette every frame).
void tex_bind_t4_raw(const void *pixels, int w, int h, const uint32_t *clut, int alpha0)
{
    s_texdesc.w = w;
    s_texdesc.h = h;
    s_texdesc.format = R_TEX_T4;
    s_texdesc.alpha0 = alpha0;
    s_texdesc.pixels = pixels;
    s_texdesc.clut = clut;
    s_texdesc.wrap = R_WRAP_REPEAT;
    s_texdesc.dynamic = 1;
    r_bind_texture(&s_texdesc);
    r_set_tex_func(R_TEXFUNC_MODULATE);
    r_set_uv_scale(1.0f, 1.0f);
    s_bound_tex = NULL;
}

static const Model *s_tex_override;

void model_set_tex_override(const Model *tex_model) { s_tex_override = tex_model; }

void model_preview(int x, int y, int w, int h, const Model *m, float yaw_rad)
{
    if (!m->file || w <= 0 || h <= 0)
        return;
    const float *lo = m->file->bounds_min, *hi = m->file->bounds_max;
    float cx = (lo[0] + hi[0]) * 0.5f, cz = (lo[2] + hi[2]) * 0.5f;
    float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    float big = dx > dy ? dx : dy;
    if (dz > big) big = dz;
    if (big < 0.001f) big = 0.001f;
    float s = 0.55f / big;
    float top = dy * s;

    r_inset_3d(1);
    gfx_viewport(x, y, w, h);
    r_matrix_mode(R_MATRIX_PROJECTION);
    r_load_identity();
    r_perspective(35.0f, (float)w / (float)h, 0.05f, 20.0f);
    r_set_near(0.05f);
    r_matrix_mode(R_MATRIX_VIEW);
    r_load_identity();
    RVec3 eye = {0.0f, top * 0.55f + 0.18f, 1.25f};
    RVec3 at = {0.0f, top * 0.5f, 0.0f};
    RVec3 up = {0.0f, 1.0f, 0.0f};
    r_look_at(&eye, &at, &up);
    r_set_depth_test(1);
    r_set_depth_write(1);
    r_set_cull(1);
    r_clear_depth(0);
    r_matrix_mode(R_MATRIX_MODEL);
    r_load_identity();
    r_rotate_y(yaw_rad);
    RVec3 sc = {s, s, s};
    r_scale(&sc);
    RVec3 off = {-cx, -lo[1], -cz};
    r_translate(&off);
    model_draw(m);
    r_set_depth_test(0);
    r_set_depth_write(0);
    gfx_viewport_screen();
    r_inset_3d(0);  // back to the 2D pass's state
    tex_invalidate();
}

static void draw_batch(const Model *m, const McmBatch *b)
{
    if (s_tex_override && b->tex == 0)
        tex_bind(s_tex_override, 0);  // e.g. a character's alternate face texture
    else
        tex_bind(m, b->tex);

    if (b->flags & MCM_BATCH_NOFOG)
        r_set_fog(0);
    if (b->flags & MCM_BATCH_ALPHA)
        r_set_alpha_test(1);
    if (b->flags & MCM_BATCH_DOUBLE)
        r_set_cull(0);
    if (b->flags & MCM_BATCH_ADDITIVE) {
        r_set_blend(R_BLEND_ADD);
        r_set_depth_write(0);
    }

    r_draw_native(R_PRIM_TRIANGLES, m->rverts + b->first_vertex, (int)b->vertex_count);
    g_rstats.triangles += b->vertex_count / 3;
    g_rstats.vertices += b->vertex_count;
    g_rstats.draw_calls++;

    if (b->flags & MCM_BATCH_NOFOG)
        r_set_fog(1);
    if (b->flags & MCM_BATCH_ALPHA)
        r_set_alpha_test(0);
    if (b->flags & MCM_BATCH_DOUBLE)
        r_set_cull(1);
    if (b->flags & MCM_BATCH_ADDITIVE) {
        r_set_blend(R_BLEND_NONE);
        r_set_depth_write(1);
    }
}

void model_draw_part(const Model *m, int part)
{
    const McmPart *p = &m->parts[part];
    for (unsigned int i = 0; i < p->batch_count; i++)
        draw_batch(m, &m->batches[p->first_batch + i]);
}

void model_draw(const Model *m)
{
    for (unsigned int i = 0; i < m->file->part_count; i++)
        model_draw_part(m, (int)i);
}

#define MAX_POSED_PARTS 16

void model_draw_posed(const Model *m, const RMat4 *root, const float (*rot)[3], int n,
                      RMat4 *out)
{
    RMat4 mats[MAX_POSED_PARTS];
    int count = (int)m->file->part_count;
    if (count > MAX_POSED_PARTS)
        count = MAX_POSED_PARTS;
    r_matrix_mode(R_MATRIX_MODEL);
    // Parts are stored parents-first, so each parent's matrix is ready in time.
    for (int i = 0; i < count; i++) {
        const McmPart *p = &m->parts[i];
        r_load_matrix(p->parent >= 0 && p->parent < i ? &mats[p->parent] : root);
        if (i < n) {
            RVec3 pv = {p->pivot[0], p->pivot[1], p->pivot[2]};
            RVec3 npv = {-p->pivot[0], -p->pivot[1], -p->pivot[2]};
            r_translate(&pv);
            r_rotate_y(rot[i][1]);
            r_rotate_x(rot[i][0]);
            r_rotate_z(rot[i][2]);
            r_translate(&npv);
        }
        r_store_matrix(&mats[i]);
        if (out && i < n)
            out[i] = mats[i];
        model_draw_part(m, i);
    }
}