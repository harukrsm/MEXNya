#ifndef MC_RENDER_PRIMITIVES_H
#define MC_RENDER_PRIMITIVES_H

#include <stdint.h>

#include "engine/core/math.h"

typedef enum {
    R_SHAPE_BOX = 0,
    R_SHAPE_PLANE,
    R_SHAPE_SPHERE,
    R_SHAPE_CYLINDER,
    R_SHAPE_CONE,
    R_SHAPE_COUNT
} RShape;

void r_draw_shape(RShape shape, MxV3 position, MxQuat rotation, MxV3 scale, uint32_t abgr);

#endif
