#ifndef MC_WORLD_INSTANTIATE_H
#define MC_WORLD_INSTANTIATE_H

#include "engine/core/json.h"
#include "engine/world/world.h"

MxWorld *mx_world_from_scene(const Json *scene);
Json *mx_world_to_json(const MxWorld *w);

#endif
