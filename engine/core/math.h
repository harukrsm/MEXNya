#ifndef MC_CORE_MATH_H
#define MC_CORE_MATH_H

#include <math.h>

#define MX_PI 3.14159265358979323846f
#define MX_DEG2RAD (MX_PI / 180.0f)

typedef struct { float x, y; } MxV2;
typedef struct { float x, y, z; } MxV3;
typedef struct { float x, y, z, w; } MxV4;
typedef struct { float m[9]; } MxMat3;
typedef struct { float m[16]; } MxMat4;
typedef struct { float x, y, z, w; } MxQuat;
typedef struct { MxV3 min, max; } MxAabb;
typedef struct { MxV3 origin, dir; } MxRay;
typedef struct { float t; MxV3 point, normal; } MxRayHit;
typedef struct { MxV3 translation; MxQuat rotation; MxV3 scale; } MxTransform;

static inline MxV2 mx_v2(float x, float y) { MxV2 v = {x, y}; return v; }
static inline MxV3 mx_v3(float x, float y, float z) { MxV3 v = {x, y, z}; return v; }
static inline MxV4 mx_v4(float x, float y, float z, float w) { MxV4 v = {x, y, z, w}; return v; }

static inline MxV2 mx_v2_add(MxV2 a, MxV2 b) { return mx_v2(a.x + b.x, a.y + b.y); }
static inline MxV2 mx_v2_sub(MxV2 a, MxV2 b) { return mx_v2(a.x - b.x, a.y - b.y); }
static inline MxV2 mx_v2_scale(MxV2 a, float s) { return mx_v2(a.x * s, a.y * s); }
static inline float mx_v2_dot(MxV2 a, MxV2 b) { return a.x * b.x + a.y * b.y; }
static inline float mx_v2_length(MxV2 a) { return sqrtf(mx_v2_dot(a, a)); }
static inline MxV2 mx_v2_normalize(MxV2 a)
{
    float l = mx_v2_length(a);
    return l > 1e-8f ? mx_v2(a.x / l, a.y / l) : mx_v2(0.0f, 0.0f);
}
static inline MxV2 mx_v2_lerp(MxV2 a, MxV2 b, float t) { return mx_v2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }

static inline MxV3 mx_v3_add(MxV3 a, MxV3 b) { return mx_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline MxV3 mx_v3_sub(MxV3 a, MxV3 b) { return mx_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline MxV3 mx_v3_scale(MxV3 a, float s) { return mx_v3(a.x * s, a.y * s, a.z * s); }
static inline float mx_v3_dot(MxV3 a, MxV3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline MxV3 mx_v3_cross(MxV3 a, MxV3 b)
{
    return mx_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float mx_v3_length(MxV3 a) { return sqrtf(mx_v3_dot(a, a)); }
static inline MxV3 mx_v3_normalize(MxV3 a)
{
    float l = mx_v3_length(a);
    return l > 1e-8f ? mx_v3(a.x / l, a.y / l, a.z / l) : mx_v3(0.0f, 0.0f, 0.0f);
}
static inline MxV3 mx_v3_lerp(MxV3 a, MxV3 b, float t)
{
    return mx_v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

static inline MxV4 mx_v4_add(MxV4 a, MxV4 b) { return mx_v4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
static inline MxV4 mx_v4_sub(MxV4 a, MxV4 b) { return mx_v4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
static inline float mx_v4_dot(MxV4 a, MxV4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline MxV4 mx_v4_scale(MxV4 a, float s) { return mx_v4(a.x * s, a.y * s, a.z * s, a.w * s); }

void mx_mat3_identity(MxMat3 *m);
void mx_mat3_mul(MxMat3 *out, const MxMat3 *a, const MxMat3 *b);
void mx_mat3_from_mat4(MxMat3 *out, const MxMat4 *m);

void mx_mat4_identity(MxMat4 *m);
void mx_mat4_mul(MxMat4 *out, const MxMat4 *a, const MxMat4 *b);
void mx_mat4_translation(MxMat4 *m, MxV3 t);
void mx_mat4_scaling(MxMat4 *m, MxV3 s);
void mx_mat4_rotation(MxMat4 *m, MxV3 axis, float radians);
void mx_mat4_rotation_x(MxMat4 *m, float radians);
void mx_mat4_rotation_y(MxMat4 *m, float radians);
void mx_mat4_rotation_z(MxMat4 *m, float radians);
void mx_mat4_perspective(MxMat4 *m, float fov_degrees, float aspect, float near_m, float far_m);
void mx_mat4_ortho(MxMat4 *m, float left, float right, float bottom, float top, float near_m, float far_m);
void mx_mat4_look_at(MxMat4 *m, MxV3 eye, MxV3 at, MxV3 up);
int mx_mat4_invert(MxMat4 *out, const MxMat4 *m);
MxV3 mx_mat4_mul_point(const MxMat4 *m, MxV3 p);
MxV3 mx_mat4_mul_dir(const MxMat4 *m, MxV3 d);
MxV4 mx_mat4_mul_vec4(const MxMat4 *m, MxV4 v);

MxQuat mx_quat_identity(void);
MxQuat mx_quat_axis_angle(MxV3 axis, float radians);
MxQuat mx_quat_mul(MxQuat a, MxQuat b);
MxQuat mx_quat_normalize(MxQuat q);
MxQuat mx_quat_from_mat4(const MxMat4 *m);
void mx_quat_to_mat4(const MxQuat *q, MxMat4 *m);
MxV3 mx_quat_rotate(MxQuat q, MxV3 v);

void mx_transform_compose(MxMat4 *out, const MxTransform *t);
int mx_transform_decompose(MxTransform *out, const MxMat4 *m);

MxAabb mx_aabb_empty(void);
MxAabb mx_aabb_from_center(MxV3 center, MxV3 half);
void mx_aabb_add_point(MxAabb *a, MxV3 p);
int mx_aabb_intersect(const MxAabb *a, const MxAabb *b);
int mx_aabb_contains(const MxAabb *a, MxV3 p);
MxAabb mx_aabb_transform(const MxAabb *a, const MxMat4 *m);
int mx_ray_aabb(const MxRay *ray, const MxAabb *a, float max_t, MxRayHit *hit);

#endif
