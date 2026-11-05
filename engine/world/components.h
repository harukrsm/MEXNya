#ifndef MC_WORLD_COMPONENTS_H
#define MC_WORLD_COMPONENTS_H

#include "engine/core/math.h"
#include "engine/world/world.h"

typedef struct {
    MxV3 position, scale;
    MxQuat rotation;
    MxV3 prev_position, prev_scale;
    MxQuat prev_rotation;
    MxEntity parent;
    MxMat4 local, world;
    int dirty;
} MxTransform;

typedef struct {
    int mesh;
    int material;
    uint32_t tint;
    int layer;
} MxMeshRenderer;

typedef enum { MX_BODY_STATIC = 0, MX_BODY_DYNAMIC, MX_BODY_KINEMATIC } MxBodyType;

typedef struct {
    int shape;
    MxV3 half_extents;
    float radius;
    uint32_t layer, mask;
    int trigger;
} MxCollider;

typedef struct {
    MxBodyType type;
    MxV3 velocity;
    float mass, damping;
    uint32_t layer, mask;
} MxRigidBody;

typedef struct {
    const char *source;
    int started;
} MxScript;

typedef struct {
    const char *clip;
    float volume;
    int loop;
    int playing;
} MxAudioSource;

typedef struct {
    float fov, near_m, far_m;
    int ortho;
} MxWorldCamera;

typedef struct {
    MxV3 color;
    float intensity, range;
} MxLight;

typedef struct {
    int texture;
    uint32_t color;
    int layer;
    float width, height;
} MxSprite;

#endif
