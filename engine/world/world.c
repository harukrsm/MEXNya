#include "engine/world/world.h"
#include "engine/world/components.h"
#include "engine/core/map.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"

#include <limits.h>
#include <string.h>

typedef struct {
    int *sparse;
    int *dense;
    unsigned char *data;
    int count, cap, sparse_cap;
    size_t stride;
} CompStore;

struct MxWorld {
    int cap, fresh, count;
    uint32_t *gen;
    unsigned char *alive;
    const char **names;
    const char **types;
    uint64_t *name_hash;
    int *name_next;
    MxTransform *tr;
    int *free_ids;
    int free_cap, free_count;
    CompStore comps[MX_COMP_COUNT];
    MxMap *name_map;
};

static size_t comp_stride(uint32_t type)
{
    switch (type) {
    case MX_COMP_MESH: return sizeof(MxMeshRenderer);
    case MX_COMP_COLLIDER: return sizeof(MxCollider);
    case MX_COMP_RIGIDBODY: return sizeof(MxRigidBody);
    case MX_COMP_SCRIPT: return sizeof(MxScript);
    case MX_COMP_AUDIO: return sizeof(MxAudioSource);
    case MX_COMP_CAMERA: return sizeof(MxWorldCamera);
    case MX_COMP_LIGHT: return sizeof(MxLight);
    case MX_COMP_SPRITE: return sizeof(MxSprite);
    default: return 0;
    }
}

static void transform_init(MxTransform *t)
{
    memset(t, 0, sizeof(*t));
    t->scale = mx_v3(1.0f, 1.0f, 1.0f);
    t->prev_scale = t->scale;
    t->rotation = mx_quat_identity();
    t->prev_rotation = t->rotation;
    t->parent = MX_ENTITY_NONE;
    mx_mat4_identity(&t->local);
    mx_mat4_identity(&t->world);
    t->dirty = 1;
}

static int world_grow(MxWorld *w, int need)
{
    if (need <= w->cap) return 1;
    int cap = w->cap > 0 ? w->cap : 64;
    while (cap < need) {
        if (cap > INT_MAX / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    uint32_t *gen = (uint32_t *)mx_mem_calloc((size_t)cap, sizeof(*gen));
    unsigned char *alive = (unsigned char *)mx_mem_calloc((size_t)cap, 1);
    const char **names = (const char **)mx_mem_calloc((size_t)cap, sizeof(*names));
    const char **types = (const char **)mx_mem_calloc((size_t)cap, sizeof(*types));
    uint64_t *hashes = (uint64_t *)mx_mem_calloc((size_t)cap, sizeof(*hashes));
    int *next = (int *)mx_mem_alloc(sizeof(int) * (size_t)cap);
    MxTransform *tr = (MxTransform *)mx_mem_calloc((size_t)cap, sizeof(*tr));
    if (!gen || !alive || !names || !types || !hashes || !next || !tr) {
        mx_mem_free(gen);
        mx_mem_free(alive);
        mx_mem_free(names);
        mx_mem_free(types);
        mx_mem_free(hashes);
        mx_mem_free(next);
        mx_mem_free(tr);
        return 0;
    }
    for (int i = 0; i < cap; i++) {
        next[i] = -1;
        transform_init(&tr[i]);
    }
    if (w->cap > 0) {
        memcpy(gen, w->gen, sizeof(uint32_t) * (size_t)w->cap);
        memcpy(alive, w->alive, (size_t)w->cap);
        memcpy(names, w->names, sizeof(char *) * (size_t)w->cap);
        memcpy(types, w->types, sizeof(char *) * (size_t)w->cap);
        memcpy(hashes, w->name_hash, sizeof(uint64_t) * (size_t)w->cap);
        memcpy(next, w->name_next, sizeof(int) * (size_t)w->cap);
        memcpy(tr, w->tr, sizeof(MxTransform) * (size_t)w->cap);
    }
    mx_mem_free(w->gen);
    mx_mem_free(w->alive);
    mx_mem_free(w->names);
    mx_mem_free(w->types);
    mx_mem_free(w->name_hash);
    mx_mem_free(w->name_next);
    mx_mem_free(w->tr);
    w->gen = gen;
    w->alive = alive;
    w->names = names;
    w->types = types;
    w->name_hash = hashes;
    w->name_next = next;
    w->tr = tr;
    for (int type = 0; type < MX_COMP_COUNT; type++) {
        CompStore *c = &w->comps[type];
        int *sparse = (int *)mx_mem_alloc(sizeof(int) * (size_t)cap);
        if (!sparse) return 0;
        for (int i = 0; i < cap; i++) sparse[i] = -1;
        mx_mem_free(c->sparse);
        c->sparse = sparse;
        c->sparse_cap = cap;
    }
    w->cap = cap;
    return 1;
}

static int sparse_ensure(MxWorld *w, CompStore *c)
{
    if (c->sparse_cap >= w->cap) return 1;
    int *sparse = (int *)mx_mem_realloc(c->sparse, sizeof(int) * (size_t)w->cap);
    if (!sparse) return 0;
    for (int i = c->sparse_cap; i < w->cap; i++) sparse[i] = -1;
    c->sparse = sparse;
    c->sparse_cap = w->cap;
    return 1;
}

static int comp_grow(CompStore *c, int need)
{
    if (need <= c->cap) return 1;
    int cap = c->cap > 0 ? c->cap : 8;
    while (cap < need) {
        if (cap > INT_MAX / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    int *dense = (int *)mx_mem_alloc(sizeof(int) * (size_t)cap);
    unsigned char *data = (unsigned char *)mx_mem_alloc(c->stride * (size_t)cap);
    if (!dense || !data) {
        mx_mem_free(dense);
        mx_mem_free(data);
        return 0;
    }
    memcpy(dense, c->dense, sizeof(int) * (size_t)c->count);
    memcpy(data, c->data, c->stride * (size_t)c->count);
    mx_mem_free(c->dense);
    mx_mem_free(c->data);
    c->dense = dense;
    c->data = data;
    c->cap = cap;
    return 1;
}

MxWorld *mx_world_create(void)
{
    MxWorld *w = (MxWorld *)mx_mem_calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->name_map = mx_map_new();
    if (!w->name_map) {
        mx_mem_free(w);
        return NULL;
    }
    for (int i = 0; i < MX_COMP_COUNT; i++) w->comps[i].stride = comp_stride((uint32_t)i);
    if (!world_grow(w, 64)) {
        mx_map_free(w->name_map);
        mx_mem_free(w);
        return NULL;
    }
    return w;
}

void mx_world_destroy(MxWorld *w)
{
    if (!w) return;
    for (int i = 0; i < MX_COMP_COUNT; i++) {
        mx_mem_free(w->comps[i].sparse);
        mx_mem_free(w->comps[i].dense);
        mx_mem_free(w->comps[i].data);
    }
    mx_mem_free(w->gen);
    mx_mem_free(w->alive);
    mx_mem_free(w->names);
    mx_mem_free(w->types);
    mx_mem_free(w->name_hash);
    mx_mem_free(w->name_next);
    mx_mem_free(w->tr);
    mx_mem_free(w->free_ids);
    mx_map_free(w->name_map);
    mx_mem_free(w);
}

static int free_push(MxWorld *w, int index)
{
    if (w->free_count >= w->free_cap) {
        int cap = w->free_cap > 0 ? w->free_cap * 2 : 64;
        int *ids = (int *)mx_mem_realloc(w->free_ids, sizeof(int) * (size_t)cap);
        if (!ids) return 0;
        w->free_ids = ids;
        w->free_cap = cap;
    }
    w->free_ids[w->free_count++] = index;
    return 1;
}

MxEntity mx_entity_create(MxWorld *w)
{
    MxEntity none = MX_ENTITY_NONE;
    int index;
    if (!w) return none;
    if (w->free_count > 0) {
        index = w->free_ids[--w->free_count];
    } else {
        if (w->fresh >= w->cap && !world_grow(w, w->fresh + 1)) return none;
        index = w->fresh++;
    }
    w->alive[index] = 1;
    w->count++;
    w->names[index] = NULL;
    w->types[index] = NULL;
    w->name_hash[index] = 0;
    w->name_next[index] = -1;
    transform_init(&w->tr[index]);
    for (int i = 0; i < MX_COMP_COUNT; i++) w->comps[i].sparse[index] = -1;
    MxEntity e = {(uint32_t)index, w->gen[index]};
    return e;
}

int mx_entity_alive(const MxWorld *w, MxEntity e)
{
    return w && e.index < (uint32_t)w->cap && w->alive[e.index] && w->gen[e.index] == e.gen;
}

static void name_unlink(MxWorld *w, int index)
{
    if (!w->names[index]) return;
    uint64_t hash = w->name_hash[index];
    int head = (int)(uintptr_t)mx_map_get(w->name_map, hash) - 1;
    if (head < 0) return;
    if (head == index) {
        int next = w->name_next[index];
        if (next >= 0) mx_map_put(w->name_map, hash, (void *)(uintptr_t)(next + 1));
        else mx_map_del(w->name_map, hash);
    } else {
        int prev = head;
        while (prev >= 0 && w->name_next[prev] != index) prev = w->name_next[prev];
        if (prev >= 0) w->name_next[prev] = w->name_next[index];
    }
    w->names[index] = NULL;
    w->name_next[index] = -1;
    w->name_hash[index] = 0;
}

void mx_entity_destroy(MxWorld *w, MxEntity e)
{
    if (!mx_entity_alive(w, e)) return;
    int index = (int)e.index;
    for (uint32_t type = 1; type < MX_COMP_COUNT; type++) mx_comp_remove(w, e, type);
    name_unlink(w, index);
    w->alive[index] = 0;
    w->gen[index]++;
    w->count--;
    free_push(w, index);
}

void mx_entity_set_name(MxWorld *w, MxEntity e, const char *name)
{
    if (!mx_entity_alive(w, e)) return;
    int index = (int)e.index;
    const char *canonical = name && name[0] ? mx_str_intern(name) : NULL;
    if (canonical == w->names[index]) return;
    name_unlink(w, index);
    if (!canonical) return;
    uint64_t hash = mx_hash_str(canonical);
    int head = (int)(uintptr_t)mx_map_get(w->name_map, hash) - 1;
    w->names[index] = canonical;
    w->name_hash[index] = hash;
    w->name_next[index] = head;
    mx_map_put(w->name_map, hash, (void *)(uintptr_t)(index + 1));
}

const char *mx_entity_name(const MxWorld *w, MxEntity e)
{
    if (!mx_entity_alive(w, e)) return "";
    const char *n = w->names[e.index];
    return n ? n : "";
}

void mx_entity_set_type(MxWorld *w, MxEntity e, const char *type)
{
    if (!mx_entity_alive(w, e)) return;
    w->types[e.index] = type && type[0] ? mx_str_intern(type) : NULL;
}

const char *mx_entity_type(const MxWorld *w, MxEntity e)
{
    if (!mx_entity_alive(w, e)) return "";
    const char *t = w->types[e.index];
    return t ? t : "";
}

MxEntity mx_entity_find(MxWorld *w, const char *name)
{
    MxEntity none = MX_ENTITY_NONE;
    if (!w || !name || !name[0]) return none;
    const char *canonical = mx_str_intern(name);
    if (!canonical) return none;
    uint64_t hash = mx_hash_str(canonical);
    int index = (int)(uintptr_t)mx_map_get(w->name_map, hash) - 1;
    while (index >= 0) {
        if (w->alive[index] && w->names[index] == canonical) {
            MxEntity e = {(uint32_t)index, w->gen[index]};
            return e;
        }
        index = w->name_next[index];
    }
    return none;
}

int mx_entity_count(const MxWorld *w) { return w ? w->count : 0; }
int mx_entity_capacity(const MxWorld *w) { return w ? w->cap : 0; }

MxEntity mx_world_entity_at(const MxWorld *w, int index)
{
    if (!w || index < 0 || index >= w->cap || !w->alive[index]) return MX_ENTITY_NONE;
    MxEntity e = {(uint32_t)index, w->gen[index]};
    return e;
}

void *mx_comp_add(MxWorld *w, MxEntity e, uint32_t type)
{
    if (!w || type >= MX_COMP_COUNT || !mx_entity_alive(w, e)) return NULL;
    int index = (int)e.index;
    if (type == MX_COMP_TRANSFORM) return &w->tr[index];
    CompStore *c = &w->comps[type];
    if (!sparse_ensure(w, c)) return NULL;
    if (c->sparse[index] >= 0) return c->data + c->stride * (size_t)c->sparse[index];
    if (!comp_grow(c, c->count + 1)) return NULL;
    int dense = c->count++;
    c->dense[dense] = index;
    c->sparse[index] = dense;
    void *p = c->data + c->stride * (size_t)dense;
    memset(p, 0, c->stride);
    return p;
}

void *mx_comp_get(const MxWorld *w, MxEntity e, uint32_t type)
{
    if (!w || type >= MX_COMP_COUNT || !mx_entity_alive(w, e)) return NULL;
    int index = (int)e.index;
    if (type == MX_COMP_TRANSFORM) return &w->tr[index];
    const CompStore *c = &w->comps[type];
    if (index >= c->sparse_cap) return NULL;
    int dense = c->sparse[index];
    if (dense < 0 || dense >= c->count || c->dense[dense] != index) return NULL;
    return c->data + c->stride * (size_t)dense;
}

int mx_comp_has(const MxWorld *w, MxEntity e, uint32_t type)
{
    return mx_comp_get(w, e, type) != NULL;
}

void mx_comp_remove(MxWorld *w, MxEntity e, uint32_t type)
{
    if (!w || type == MX_COMP_TRANSFORM || type >= MX_COMP_COUNT || !mx_entity_alive(w, e)) return;
    int index = (int)e.index;
    CompStore *c = &w->comps[type];
    if (index >= c->sparse_cap) return;
    int dense = c->sparse[index];
    if (dense < 0 || dense >= c->count) return;
    int last = c->count - 1;
    if (dense != last) {
        int moved = c->dense[last];
        memcpy(c->data + c->stride * (size_t)dense, c->data + c->stride * (size_t)last, c->stride);
        c->dense[dense] = moved;
        c->sparse[moved] = dense;
    }
    c->count--;
    c->sparse[index] = -1;
}

MxEntity mx_entity_parent(const MxWorld *w, MxEntity e)
{
    if (!mx_entity_alive(w, e)) return MX_ENTITY_NONE;
    return w->tr[e.index].parent;
}

void mx_entity_set_parent(MxWorld *w, MxEntity e, MxEntity parent)
{
    if (!mx_entity_alive(w, e)) return;
    w->tr[e.index].parent = mx_entity_alive(w, parent) ? parent : MX_ENTITY_NONE;
}

void mx_world_update_transforms(MxWorld *w)
{
    if (!w || w->cap == 0) return;
    unsigned char *done = (unsigned char *)mx_mem_calloc((size_t)w->cap, 1);
    int *stack = (int *)mx_mem_alloc(sizeof(int) * (size_t)w->cap);
    if (!done || !stack) {
        mx_mem_free(done);
        mx_mem_free(stack);
        return;
    }
    for (int i = 0; i < w->cap; i++) {
        if (!w->alive[i] || done[i]) continue;
        int top = 0;
        int at = i;
        while (at >= 0 && at < w->cap && w->alive[at] && !done[at] && top < w->cap) {
            stack[top++] = at;
            MxEntity parent = w->tr[at].parent;
            if (!mx_entity_alive(w, parent)) break;
            at = (int)parent.index;
        }
        while (top > 0) {
            int index = stack[--top];
            MxTransform *t = &w->tr[index];
            MxTrs xf;
            xf.translation = t->position;
            xf.rotation = t->rotation;
            xf.scale = t->scale;
            mx_transform_compose(&t->local, &xf);
            MxEntity parent = t->parent;
            if (mx_entity_alive(w, parent))
                mx_mat4_mul(&t->world, &w->tr[parent.index].world, &t->local);
            else
                t->world = t->local;
            t->dirty = 0;
            done[index] = 1;
        }
    }
    mx_mem_free(done);
    mx_mem_free(stack);
}

void mx_world_tick(MxWorld *w, float dt)
{
    (void)dt;
    if (!w) return;
    for (int i = 0; i < w->cap; i++) {
        if (!w->alive[i]) continue;
        MxTransform *t = &w->tr[i];
        t->prev_position = t->position;
        t->prev_rotation = t->rotation;
        t->prev_scale = t->scale;
    }
    mx_world_update_transforms(w);
}
