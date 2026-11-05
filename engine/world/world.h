#ifndef MC_WORLD_WORLD_H
#define MC_WORLD_WORLD_H

#include <stdint.h>

typedef struct { uint32_t index, gen; } MxEntity;

#define MX_ENTITY_NONE ((MxEntity){0xFFFFFFFFu, 0})

typedef enum {
    MX_COMP_TRANSFORM = 0,
    MX_COMP_MESH,
    MX_COMP_COLLIDER,
    MX_COMP_RIGIDBODY,
    MX_COMP_SCRIPT,
    MX_COMP_AUDIO,
    MX_COMP_CAMERA,
    MX_COMP_LIGHT,
    MX_COMP_SPRITE,
    MX_COMP_COUNT
} MxComponentType;

typedef struct MxWorld MxWorld;

MxWorld *mx_world_create(void);
void mx_world_destroy(MxWorld *w);
MxEntity mx_entity_create(MxWorld *w);
void mx_entity_destroy(MxWorld *w, MxEntity e);
int mx_entity_alive(const MxWorld *w, MxEntity e);
MxEntity mx_entity_find(MxWorld *w, const char *name);
void mx_entity_set_name(MxWorld *w, MxEntity e, const char *name);
const char *mx_entity_name(const MxWorld *w, MxEntity e);
int mx_entity_count(const MxWorld *w);
int mx_entity_capacity(const MxWorld *w);

void *mx_comp_add(MxWorld *w, MxEntity e, uint32_t type);
void *mx_comp_get(const MxWorld *w, MxEntity e, uint32_t type);
int mx_comp_has(const MxWorld *w, MxEntity e, uint32_t type);
void mx_comp_remove(MxWorld *w, MxEntity e, uint32_t type);

void mx_world_update_transforms(MxWorld *w);
void mx_world_tick(MxWorld *w, float dt);

MxEntity mx_entity_parent(const MxWorld *w, MxEntity e);
void mx_entity_set_parent(MxWorld *w, MxEntity e, MxEntity parent);

#endif
