#include "engine/core/math.h"

#include <string.h>

void mx_mat3_identity(MxMat3 *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0] = m->m[4] = m->m[8] = 1.0f;
}

void mx_mat3_mul(MxMat3 *out, const MxMat3 *a, const MxMat3 *b)
{
    MxMat3 t;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float v = 0.0f;
            for (int k = 0; k < 3; k++) v += a->m[k * 3 + j] * b->m[i * 3 + k];
            t.m[i * 3 + j] = v;
        }
    *out = t;
}

void mx_mat3_from_mat4(MxMat3 *out, const MxMat4 *m)
{
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++) out->m[c * 3 + r] = m->m[c * 4 + r];
}

void mx_mat4_identity(MxMat4 *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

void mx_mat4_mul(MxMat4 *out, const MxMat4 *a, const MxMat4 *b)
{
    MxMat4 t;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float v = 0.0f;
            for (int k = 0; k < 4; k++) v += a->m[k * 4 + j] * b->m[i * 4 + k];
            t.m[i * 4 + j] = v;
        }
    *out = t;
}

void mx_mat4_translation(MxMat4 *m, MxV3 t)
{
    mx_mat4_identity(m);
    m->m[12] = t.x;
    m->m[13] = t.y;
    m->m[14] = t.z;
}

void mx_mat4_scaling(MxMat4 *m, MxV3 s)
{
    memset(m, 0, sizeof(*m));
    m->m[0] = s.x;
    m->m[5] = s.y;
    m->m[10] = s.z;
    m->m[15] = 1.0f;
}

void mx_mat4_rotation(MxMat4 *m, MxV3 axis, float radians)
{
    MxV3 a = mx_v3_normalize(axis);
    float c = cosf(radians), s = sinf(radians), ic = 1.0f - c;
    mx_mat4_identity(m);
    m->m[0] = c + a.x * a.x * ic;
    m->m[1] = a.y * a.x * ic + a.z * s;
    m->m[2] = a.z * a.x * ic - a.y * s;
    m->m[4] = a.x * a.y * ic - a.z * s;
    m->m[5] = c + a.y * a.y * ic;
    m->m[6] = a.z * a.y * ic + a.x * s;
    m->m[8] = a.x * a.z * ic + a.y * s;
    m->m[9] = a.y * a.z * ic - a.x * s;
    m->m[10] = c + a.z * a.z * ic;
}

void mx_mat4_rotation_x(MxMat4 *m, float radians)
{
    mx_mat4_identity(m);
    float c = cosf(radians), s = sinf(radians);
    m->m[5] = c;
    m->m[6] = s;
    m->m[9] = -s;
    m->m[10] = c;
}

void mx_mat4_rotation_y(MxMat4 *m, float radians)
{
    mx_mat4_identity(m);
    float c = cosf(radians), s = sinf(radians);
    m->m[0] = c;
    m->m[2] = -s;
    m->m[8] = s;
    m->m[10] = c;
}

void mx_mat4_rotation_z(MxMat4 *m, float radians)
{
    mx_mat4_identity(m);
    float c = cosf(radians), s = sinf(radians);
    m->m[0] = c;
    m->m[1] = s;
    m->m[4] = -s;
    m->m[5] = c;
}

void mx_mat4_perspective(MxMat4 *m, float fov_degrees, float aspect, float near_m, float far_m)
{
    float a = (fov_degrees * 0.5f) * MX_DEG2RAD;
    float cot = cosf(a) / sinf(a);
    float dz = near_m - far_m;
    mx_mat4_identity(m);
    m->m[0] = cot / aspect;
    m->m[5] = cot;
    m->m[10] = (far_m + near_m) / dz;
    m->m[11] = -1.0f;
    m->m[14] = 2.0f * far_m * near_m / dz;
    m->m[15] = 0.0f;
}

void mx_mat4_ortho(MxMat4 *m, float left, float right, float bottom, float top, float near_m, float far_m)
{
    mx_mat4_identity(m);
    m->m[0] = 2.0f / (right - left);
    m->m[5] = 2.0f / (top - bottom);
    m->m[10] = -2.0f / (far_m - near_m);
    m->m[12] = -(right + left) / (right - left);
    m->m[13] = -(top + bottom) / (top - bottom);
    m->m[14] = -(far_m + near_m) / (far_m - near_m);
}

void mx_mat4_look_at(MxMat4 *m, MxV3 eye, MxV3 at, MxV3 up)
{
    MxV3 f = mx_v3_normalize(mx_v3_sub(at, eye));
    MxV3 s = mx_v3_normalize(mx_v3_cross(f, up));
    MxV3 u = mx_v3_cross(s, f);
    mx_mat4_identity(m);
    m->m[0] = s.x;
    m->m[4] = s.y;
    m->m[8] = s.z;
    m->m[1] = u.x;
    m->m[5] = u.y;
    m->m[9] = u.z;
    m->m[2] = -f.x;
    m->m[6] = -f.y;
    m->m[10] = -f.z;
    m->m[12] = -mx_v3_dot(s, eye);
    m->m[13] = -mx_v3_dot(u, eye);
    m->m[14] = mx_v3_dot(f, eye);
}

int mx_mat4_invert(MxMat4 *out, const MxMat4 *m)
{
    const float *a = m->m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
             a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
             a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
             a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
              a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
             a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
             a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
             a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
              a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
             a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
              a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
              a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
             a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (fabsf(det) < 1e-12f) return 0;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) out->m[i] = inv[i] * det;
    return 1;
}

MxV3 mx_mat4_mul_point(const MxMat4 *m, MxV3 p)
{
    const float *a = m->m;
    float x = a[0] * p.x + a[4] * p.y + a[8] * p.z + a[12];
    float y = a[1] * p.x + a[5] * p.y + a[9] * p.z + a[13];
    float z = a[2] * p.x + a[6] * p.y + a[10] * p.z + a[14];
    float w = a[3] * p.x + a[7] * p.y + a[11] * p.z + a[15];
    if (w != 0.0f && w != 1.0f) {
        x /= w;
        y /= w;
        z /= w;
    }
    return mx_v3(x, y, z);
}

MxV3 mx_mat4_mul_dir(const MxMat4 *m, MxV3 d)
{
    const float *a = m->m;
    return mx_v3(a[0] * d.x + a[4] * d.y + a[8] * d.z, a[1] * d.x + a[5] * d.y + a[9] * d.z,
                 a[2] * d.x + a[6] * d.y + a[10] * d.z);
}

MxV4 mx_mat4_mul_vec4(const MxMat4 *m, MxV4 v)
{
    const float *a = m->m;
    return mx_v4(a[0] * v.x + a[4] * v.y + a[8] * v.z + a[12] * v.w,
                 a[1] * v.x + a[5] * v.y + a[9] * v.z + a[13] * v.w,
                 a[2] * v.x + a[6] * v.y + a[10] * v.z + a[14] * v.w,
                 a[3] * v.x + a[7] * v.y + a[11] * v.z + a[15] * v.w);
}

MxQuat mx_quat_identity(void) { MxQuat q = {0.0f, 0.0f, 0.0f, 1.0f}; return q; }

MxQuat mx_quat_axis_angle(MxV3 axis, float radians)
{
    MxV3 a = mx_v3_normalize(axis);
    float s = sinf(radians * 0.5f);
    MxQuat q = {a.x * s, a.y * s, a.z * s, cosf(radians * 0.5f)};
    return q;
}

MxQuat mx_quat_mul(MxQuat a, MxQuat b)
{
    MxQuat q = {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    return q;
}

MxQuat mx_quat_normalize(MxQuat q)
{
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l > 1e-8f) {
        q.x /= l;
        q.y /= l;
        q.z /= l;
        q.w /= l;
    } else {
        q = mx_quat_identity();
    }
    return q;
}

MxQuat mx_quat_from_mat4(const MxMat4 *m)
{
    const float *a = m->m;
    MxQuat q;
    float trace = a[0] + a[5] + a[10];
    if (trace > 0.0f) {
        float s = sqrtf(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (a[6] - a[9]) / s;
        q.y = (a[8] - a[2]) / s;
        q.z = (a[1] - a[4]) / s;
    } else if (a[0] > a[5] && a[0] > a[10]) {
        float s = sqrtf(1.0f + a[0] - a[5] - a[10]) * 2.0f;
        q.w = (a[6] - a[9]) / s;
        q.x = 0.25f * s;
        q.y = (a[4] + a[1]) / s;
        q.z = (a[8] + a[2]) / s;
    } else if (a[5] > a[10]) {
        float s = sqrtf(1.0f + a[5] - a[0] - a[10]) * 2.0f;
        q.w = (a[8] - a[2]) / s;
        q.x = (a[4] + a[1]) / s;
        q.y = 0.25f * s;
        q.z = (a[9] + a[6]) / s;
    } else {
        float s = sqrtf(1.0f + a[10] - a[0] - a[5]) * 2.0f;
        q.w = (a[1] - a[4]) / s;
        q.x = (a[8] + a[2]) / s;
        q.y = (a[9] + a[6]) / s;
        q.z = 0.25f * s;
    }
    return mx_quat_normalize(q);
}

void mx_quat_to_mat4(const MxQuat *q, MxMat4 *m)
{
    float x = q->x, y = q->y, z = q->z, w = q->w;
    mx_mat4_identity(m);
    m->m[0] = 1.0f - 2.0f * (y * y + z * z);
    m->m[1] = 2.0f * (x * y + z * w);
    m->m[2] = 2.0f * (x * z - y * w);
    m->m[4] = 2.0f * (x * y - z * w);
    m->m[5] = 1.0f - 2.0f * (x * x + z * z);
    m->m[6] = 2.0f * (y * z + x * w);
    m->m[8] = 2.0f * (x * z + y * w);
    m->m[9] = 2.0f * (y * z - x * w);
    m->m[10] = 1.0f - 2.0f * (x * x + y * y);
}

MxV3 mx_quat_rotate(MxQuat q, MxV3 v)
{
    MxV3 u = mx_v3(q.x, q.y, q.z);
    MxV3 t = mx_v3_scale(mx_v3_cross(u, v), 2.0f);
    return mx_v3_add(v, mx_v3_add(mx_v3_scale(t, q.w), mx_v3_cross(u, t)));
}

void mx_transform_compose(MxMat4 *out, const MxTransform *t)
{
    MxMat4 r, s, p;
    mx_quat_to_mat4(&t->rotation, &r);
    mx_mat4_scaling(&s, t->scale);
    mx_mat4_mul(&r, &r, &s);
    mx_mat4_translation(&p, t->translation);
    mx_mat4_mul(out, &p, &r);
}

int mx_transform_decompose(MxTransform *out, const MxMat4 *m)
{
    const float *a = m->m;
    MxV3 c0 = mx_v3(a[0], a[1], a[2]);
    MxV3 c1 = mx_v3(a[4], a[5], a[6]);
    MxV3 c2 = mx_v3(a[8], a[9], a[10]);
    float sx = mx_v3_length(c0), sy = mx_v3_length(c1), sz = mx_v3_length(c2);
    if (sx < 1e-6f || sy < 1e-6f || sz < 1e-6f) return 0;
    out->translation = mx_v3(a[12], a[13], a[14]);
    c0 = mx_v3_scale(c0, 1.0f / sx);
    c1 = mx_v3_scale(c1, 1.0f / sy);
    c2 = mx_v3_scale(c2, 1.0f / sz);
    if (mx_v3_dot(mx_v3_cross(c0, c1), c2) < 0.0f) {
        sx = -sx;
        c0 = mx_v3_scale(c0, -1.0f);
    }
    out->scale = mx_v3(sx, sy, sz);
    MxMat4 r;
    mx_mat4_identity(&r);
    r.m[0] = c0.x; r.m[1] = c0.y; r.m[2] = c0.z;
    r.m[4] = c1.x; r.m[5] = c1.y; r.m[6] = c1.z;
    r.m[8] = c2.x; r.m[9] = c2.y; r.m[10] = c2.z;
    out->rotation = mx_quat_from_mat4(&r);
    return 1;
}

MxAabb mx_aabb_empty(void)
{
    MxAabb a = {{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    return a;
}

MxAabb mx_aabb_from_center(MxV3 center, MxV3 half)
{
    MxAabb a = {mx_v3_sub(center, half), mx_v3_add(center, half)};
    return a;
}

void mx_aabb_add_point(MxAabb *a, MxV3 p)
{
    if (p.x < a->min.x) a->min.x = p.x;
    if (p.y < a->min.y) a->min.y = p.y;
    if (p.z < a->min.z) a->min.z = p.z;
    if (p.x > a->max.x) a->max.x = p.x;
    if (p.y > a->max.y) a->max.y = p.y;
    if (p.z > a->max.z) a->max.z = p.z;
}

int mx_aabb_intersect(const MxAabb *a, const MxAabb *b)
{
    return a->min.x <= b->max.x && a->max.x >= b->min.x && a->min.y <= b->max.y && a->max.y >= b->min.y &&
           a->min.z <= b->max.z && a->max.z >= b->min.z;
}

int mx_aabb_contains(const MxAabb *a, MxV3 p)
{
    return p.x >= a->min.x && p.x <= a->max.x && p.y >= a->min.y && p.y <= a->max.y && p.z >= a->min.z &&
           p.z <= a->max.z;
}

MxAabb mx_aabb_transform(const MxAabb *a, const MxMat4 *m)
{
    MxAabb out = mx_aabb_empty();
    for (int i = 0; i < 8; i++) {
        MxV3 p = mx_v3((i & 1) ? a->max.x : a->min.x, (i & 2) ? a->max.y : a->min.y,
                       (i & 4) ? a->max.z : a->min.z);
        mx_aabb_add_point(&out, mx_mat4_mul_point(m, p));
    }
    return out;
}

int mx_ray_aabb(const MxRay *ray, const MxAabb *a, float max_t, MxRayHit *hit)
{
    float tmin = 0.0f, tmax = max_t;
    int axis = -1, sign = 0;
    float o[3] = {ray->origin.x, ray->origin.y, ray->origin.z};
    float d[3] = {ray->dir.x, ray->dir.y, ray->dir.z};
    float lo[3] = {a->min.x, a->min.y, a->min.z};
    float hi[3] = {a->max.x, a->max.y, a->max.z};
    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-8f) {
            if (o[i] < lo[i] || o[i] > hi[i]) return 0;
            continue;
        }
        float t1 = (lo[i] - o[i]) / d[i];
        float t2 = (hi[i] - o[i]) / d[i];
        int s = -1;
        if (t1 > t2) {
            float tmp = t1;
            t1 = t2;
            t2 = tmp;
            s = 1;
        }
        if (t1 > tmin) {
            tmin = t1;
            axis = i;
            sign = s;
        }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return 0;
    }
    if (hit) {
        hit->t = tmin;
        hit->point = mx_v3_add(ray->origin, mx_v3_scale(ray->dir, tmin));
        hit->normal = mx_v3(0.0f, 0.0f, 0.0f);
        if (axis >= 0) {
            float *n = &hit->normal.x;
            n[axis] = (float)sign;
        }
    }
    return 1;
}
