#ifndef MC_WORLD_DRAW_H
#define MC_WORLD_DRAW_H

#include "engine/world/world.h"

void mx_world_draw_3d(const MxWorld *w);
void mx_world_draw_2d(const MxWorld *w, int x, int y, float scale);

#endif
