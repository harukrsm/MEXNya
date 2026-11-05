#include "engine/render/primitives.h"
#include "engine/render/render_api.h"

#include <math.h>
#include <string.h>

typedef struct { float p[3], n[3]; } PrimVertex;

#define PRIM_MAX 600

typedef struct { PrimVertex v[PRIM_MAX]; int n; } PrimMesh;

static PrimMesh s_mesh[R_SHAPE_COUNT];
static RVertex s_buf[PRIM_MAX];

static void v3(float *o, float x, float y, float z)
{
    o[0] = x;
    o[1] = y;
    o[2] = z;
}

static void tri(PrimMesh *m, const float *a, const float *b, const float *c)
{
    if (m->n + 3 > PRIM_MAX) return;
    float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l > 0) {
        n[0] /= l;
        n[1] /= l;
        n[2] /= l;
    }
    const float *pts[3] = {a, b, c};
    for (int i = 0; i < 3; i++, m->n++) {
        memcpy(m->v[m->n].p, pts[i], 12);
        memcpy(m->v[m->n].n, n, 12);
    }
}

static void quad(PrimMesh *m, const float *a, const float *b, const float *c, const float *d)
{
    tri(m, a, b, c);
    tri(m, a, c, d);
}

static void build(void)
{
    if (s_mesh[R_SHAPE_BOX].n) return;
    float c[8][3];
    for (int i = 0; i < 8; i++)
        v3(c[i], (i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
    PrimMesh *b = &s_mesh[R_SHAPE_BOX];
    quad(b, c[0], c[2], c[3], c[1]);
    quad(b, c[4], c[5], c[7], c[6]);
    quad(b, c[0], c[4], c[6], c[2]);
    quad(b, c[1], c[3], c[7], c[5]);
    quad(b, c[2], c[6], c[7], c[3]);
    quad(b, c[0], c[1], c[5], c[4]);
    float p0[3] = {-0.5f, 0, -0.5f}, p1[3] = {0.5f, 0, -0.5f}, p2[3] = {0.5f, 0, 0.5f}, p3[3] = {-0.5f, 0, 0.5f};
    quad(&s_mesh[R_SHAPE_PLANE], p0, p3, p2, p1);
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
            quad(&s_mesh[R_SHAPE_SPHERE], q[0], q[3], q[2], q[1]);
        }
    for (int s = 0; s < SEG; s++) {
        float a0 = 6.2831853f * s / SEG, a1 = 6.2831853f * (s + 1) / SEG;
        float b0[3] = {cosf(a0) * 0.5f, -0.5f, sinf(a0) * 0.5f};
        float b1[3] = {cosf(a1) * 0.5f, -0.5f, sinf(a1) * 0.5f};
        float t0[3] = {b0[0], 0.5f, b0[2]}, t1[3] = {b1[0], 0.5f, b1[2]};
        float top[3] = {0, 0.5f, 0}, bot[3] = {0, -0.5f, 0};
        quad(&s_mesh[R_SHAPE_CYLINDER], b0, t0, t1, b1);
        tri(&s_mesh[R_SHAPE_CYLINDER], top, t1, t0);
        tri(&s_mesh[R_SHAPE_CYLINDER], bot, b0, b1);
        tri(&s_mesh[R_SHAPE_CONE], b0, top, b1);
        tri(&s_mesh[R_SHAPE_CONE], bot, b0, b1);
    }
}

void r_draw_shape(RShape shape, MxV3 position, MxQuat rotation, MxV3 scale, uint32_t abgr)
{
    if (shape < 0 || shape >= R_SHAPE_COUNT) return;
    build();
    const PrimMesh *m = &s_mesh[shape];
    MxMat4 r;
    float sx = fabsf(scale.x) < 1e-6f ? 1e-6f : scale.x;
    float sy = fabsf(scale.y) < 1e-6f ? 1e-6f : scale.y;
    float sz = fabsf(scale.z) < 1e-6f ? 1e-6f : scale.z;
    mx_quat_to_mat4(&rotation, &r);
    static const float L[3] = {0.42f, 0.81f, 0.40f};
    unsigned rr = abgr & 0xFFu, gg = (abgr >> 8) & 0xFFu, bb = (abgr >> 16) & 0xFFu;
    for (int i = 0; i < m->n; i++) {
        float lp[3] = {m->v[i].p[0] * sx, m->v[i].p[1] * sy, m->v[i].p[2] * sz};
        float ln[3] = {m->v[i].n[0] / sx, m->v[i].n[1] / sy, m->v[i].n[2] / sz};
        float wp[3], wn[3];
        wp[0] = position.x + r.m[0] * lp[0] + r.m[4] * lp[1] + r.m[8] * lp[2];
        wp[1] = position.y + r.m[1] * lp[0] + r.m[5] * lp[1] + r.m[9] * lp[2];
        wp[2] = position.z + r.m[2] * lp[0] + r.m[6] * lp[1] + r.m[10] * lp[2];
        wn[0] = r.m[0] * ln[0] + r.m[4] * ln[1] + r.m[8] * ln[2];
        wn[1] = r.m[1] * ln[0] + r.m[5] * ln[1] + r.m[9] * ln[2];
        wn[2] = r.m[2] * ln[0] + r.m[6] * ln[1] + r.m[10] * ln[2];
        float l = sqrtf(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
        float d = l > 0 ? (wn[0] * L[0] + wn[1] * L[1] + wn[2] * L[2]) / l : 0;
        float shade = 0.42f + 0.58f * (d > 0 ? d : 0);
        int cr = (int)((float)rr * shade), cg = (int)((float)gg * shade), cb = (int)((float)bb * shade);
        s_buf[i] = (RVertex){wp[0], wp[1], wp[2], 0, 0,
                            0xFF000000u | (unsigned)cb << 16 | (unsigned)cg << 8 | (unsigned)cr};
    }
    r_bind_texture(NULL);
    r_draw(R_PRIM_TRIANGLES, s_buf, m->n);
}
