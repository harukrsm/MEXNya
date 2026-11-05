#include "engine/game/save.h"
#include "engine/world/components.h"
#include "engine/core/error.h"
#include "engine/core/map.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/platform/platform.h"

#include <stdio.h>
#include <string.h>

#define SAVE_VERSION 1u
#define SAVE_MAX_ENTITIES 1000000u
#define SAVE_MAX_STRING 4095u

typedef struct {
    unsigned char *data;
    size_t len, cap;
    int oom;
} Buf;

typedef struct {
    const unsigned char *data;
    size_t len, at;
    int bad;
} Rd;

static void put_bytes(Buf *b, const void *p, size_t n)
{
    if (b->oom) return;
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n) {
            if (cap > (size_t)-1 / 2) {
                b->oom = 1;
                return;
            }
            cap *= 2;
        }
        unsigned char *p2 = (unsigned char *)mx_mem_realloc(b->data, cap);
        if (!p2) {
            b->oom = 1;
            return;
        }
        b->data = p2;
        b->cap = cap;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void put_u32(Buf *b, uint32_t v)
{
    unsigned char p[4] = {(unsigned char)(v & 0xFFu), (unsigned char)((v >> 8) & 0xFFu),
                          (unsigned char)((v >> 16) & 0xFFu), (unsigned char)((v >> 24) & 0xFFu)};
    put_bytes(b, p, 4);
}

static void put_f32(Buf *b, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    put_u32(b, u);
}

static void put_str(Buf *b, const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n > SAVE_MAX_STRING) n = SAVE_MAX_STRING;
    put_u32(b, (uint32_t)n);
    put_bytes(b, s, n);
}

static int get_u32(Rd *r, uint32_t *out)
{
    if (r->bad || r->at + 4 > r->len) {
        r->bad = 1;
        return 0;
    }
    const unsigned char *p = r->data + r->at;
    *out = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    r->at += 4;
    return 1;
}

static int get_f32(Rd *r, float *out)
{
    uint32_t u;
    if (!get_u32(r, &u)) return 0;
    memcpy(out, &u, 4);
    return 1;
}

static int get_str(Rd *r, char *out, size_t size)
{
    uint32_t n;
    if (!get_u32(r, &n) || n > SAVE_MAX_STRING || r->at + n > r->len || n + 1 > size) return 0;
    memcpy(out, r->data + r->at, n);
    out[n] = 0;
    r->at += n;
    return 1;
}

static uint32_t crc32(const unsigned char *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

int mx_save_path(char *out, int size, const char *slot)
{
    if (!out || !slot || !slot[0]) return -1;
    if (!strcmp(slot, ".") || !strcmp(slot, "..")) return -1;
    for (const char *c = slot; *c; c++)
        if (*c == '/' || *c == '\\' || *c == ':') return -1;
    char dir[1024];
    if (plat_user_dir(dir, (int)sizeof(dir)) != 0) return -1;
    snprintf(out, (size_t)size, "%s/saves", dir);
    if (plat_mkdirs(out) != 0) return -1;
    size_t n = strlen(out);
    snprintf(out + n, (size_t)size - n, "/%s.mxs", slot);
    return 0;
}

static uint32_t component_mask(const MxWorld *w, MxEntity e)
{
    uint32_t mask = 0;
    for (uint32_t t = 1; t < MX_COMP_COUNT; t++)
        if (mx_comp_get(w, e, t)) mask |= (uint32_t)1 << t;
    return mask;
}

static void write_components(const MxWorld *w, MxEntity e, Buf *b)
{
    const MxMeshRenderer *m = (const MxMeshRenderer *)mx_comp_get(w, e, MX_COMP_MESH);
    if (m) {
        put_u32(b, (uint32_t)m->mesh);
        put_u32(b, (uint32_t)m->material);
        put_u32(b, m->tint);
        put_u32(b, (uint32_t)m->layer);
        put_str(b, m->asset);
    }
    const MxCollider *c = (const MxCollider *)mx_comp_get(w, e, MX_COMP_COLLIDER);
    if (c) {
        put_u32(b, (uint32_t)c->shape);
        put_f32(b, c->half_extents.x);
        put_f32(b, c->half_extents.y);
        put_f32(b, c->half_extents.z);
        put_f32(b, c->radius);
        put_u32(b, c->layer);
        put_u32(b, c->mask);
        put_u32(b, (uint32_t)c->trigger);
    }
    const MxRigidBody *rb = (const MxRigidBody *)mx_comp_get(w, e, MX_COMP_RIGIDBODY);
    if (rb) {
        put_u32(b, (uint32_t)rb->type);
        put_f32(b, rb->velocity.x);
        put_f32(b, rb->velocity.y);
        put_f32(b, rb->velocity.z);
        put_f32(b, rb->mass);
        put_f32(b, rb->damping);
        put_u32(b, rb->layer);
        put_u32(b, rb->mask);
    }
    const MxScript *sc = (const MxScript *)mx_comp_get(w, e, MX_COMP_SCRIPT);
    if (sc) {
        put_str(b, sc->source);
        put_u32(b, (uint32_t)sc->started);
    }
    const MxAudioSource *au = (const MxAudioSource *)mx_comp_get(w, e, MX_COMP_AUDIO);
    if (au) {
        put_str(b, au->clip);
        put_f32(b, au->volume);
        put_u32(b, (uint32_t)au->loop);
        put_u32(b, (uint32_t)au->playing);
    }
    const MxWorldCamera *cam = (const MxWorldCamera *)mx_comp_get(w, e, MX_COMP_CAMERA);
    if (cam) {
        put_f32(b, cam->fov);
        put_f32(b, cam->near_m);
        put_f32(b, cam->far_m);
        put_u32(b, (uint32_t)cam->ortho);
    }
    const MxLight *li = (const MxLight *)mx_comp_get(w, e, MX_COMP_LIGHT);
    if (li) {
        put_f32(b, li->color.x);
        put_f32(b, li->color.y);
        put_f32(b, li->color.z);
        put_f32(b, li->intensity);
        put_f32(b, li->range);
    }
    const MxSprite *sp = (const MxSprite *)mx_comp_get(w, e, MX_COMP_SPRITE);
    if (sp) {
        put_u32(b, (uint32_t)sp->kind);
        put_u32(b, (uint32_t)sp->texture);
        put_u32(b, sp->color);
        put_u32(b, (uint32_t)sp->layer);
        for (int i = 0; i < 4; i++) put_f32(b, sp->rect[i]);
        put_str(b, sp->text);
        put_str(b, sp->image);
    }
}

static int read_components(MxWorld *w, MxEntity e, uint32_t mask, Rd *r)
{
    if (mask & ((uint32_t)1 << MX_COMP_MESH)) {
        uint32_t mesh, material, tint, layer;
        char asset[1024];
        if (!get_u32(r, &mesh) || !get_u32(r, &material) || !get_u32(r, &tint) || !get_u32(r, &layer) ||
            !get_str(r, asset, sizeof(asset)))
            return 0;
        MxMeshRenderer *m = (MxMeshRenderer *)mx_comp_add(w, e, MX_COMP_MESH);
        if (!m) return 0;
        m->mesh = (int)mesh;
        m->material = (int)material;
        m->tint = tint;
        m->layer = (int)layer;
        m->asset = asset[0] ? mx_str_intern(asset) : NULL;
    }
    if (mask & ((uint32_t)1 << MX_COMP_COLLIDER)) {
        uint32_t shape, layer, cmask, trigger;
        float hx, hy, hz, radius;
        if (!get_u32(r, &shape) || !get_f32(r, &hx) || !get_f32(r, &hy) || !get_f32(r, &hz) ||
            !get_f32(r, &radius) || !get_u32(r, &layer) || !get_u32(r, &cmask) || !get_u32(r, &trigger))
            return 0;
        MxCollider *c = (MxCollider *)mx_comp_add(w, e, MX_COMP_COLLIDER);
        if (!c) return 0;
        c->shape = (int)shape;
        c->half_extents = mx_v3(hx, hy, hz);
        c->radius = radius;
        c->layer = layer;
        c->mask = cmask;
        c->trigger = (int)trigger;
    }
    if (mask & ((uint32_t)1 << MX_COMP_RIGIDBODY)) {
        uint32_t type, layer, rmask;
        float vx, vy, vz, mass, damping;
        if (!get_u32(r, &type) || !get_f32(r, &vx) || !get_f32(r, &vy) || !get_f32(r, &vz) ||
            !get_f32(r, &mass) || !get_f32(r, &damping) || !get_u32(r, &layer) || !get_u32(r, &rmask))
            return 0;
        MxRigidBody *rb = (MxRigidBody *)mx_comp_add(w, e, MX_COMP_RIGIDBODY);
        if (!rb) return 0;
        rb->type = (MxBodyType)type;
        rb->velocity = mx_v3(vx, vy, vz);
        rb->mass = mass;
        rb->damping = damping;
        rb->layer = layer;
        rb->mask = rmask;
    }
    if (mask & ((uint32_t)1 << MX_COMP_SCRIPT)) {
        char source[1024];
        uint32_t started;
        if (!get_str(r, source, sizeof(source)) || !get_u32(r, &started)) return 0;
        MxScript *sc = (MxScript *)mx_comp_add(w, e, MX_COMP_SCRIPT);
        if (!sc) return 0;
        sc->source = source[0] ? mx_str_intern(source) : NULL;
        sc->started = (int)started;
    }
    if (mask & ((uint32_t)1 << MX_COMP_AUDIO)) {
        char clip[1024];
        float volume;
        uint32_t loop, playing;
        if (!get_str(r, clip, sizeof(clip)) || !get_f32(r, &volume) || !get_u32(r, &loop) || !get_u32(r, &playing))
            return 0;
        MxAudioSource *au = (MxAudioSource *)mx_comp_add(w, e, MX_COMP_AUDIO);
        if (!au) return 0;
        au->clip = clip[0] ? mx_str_intern(clip) : NULL;
        au->volume = volume;
        au->loop = (int)loop;
        au->playing = (int)playing;
    }
    if (mask & ((uint32_t)1 << MX_COMP_CAMERA)) {
        float fov, near_m, far_m;
        uint32_t ortho;
        if (!get_f32(r, &fov) || !get_f32(r, &near_m) || !get_f32(r, &far_m) || !get_u32(r, &ortho)) return 0;
        MxWorldCamera *cam = (MxWorldCamera *)mx_comp_add(w, e, MX_COMP_CAMERA);
        if (!cam) return 0;
        cam->fov = fov;
        cam->near_m = near_m;
        cam->far_m = far_m;
        cam->ortho = (int)ortho;
    }
    if (mask & ((uint32_t)1 << MX_COMP_LIGHT)) {
        float x, y, z, intensity, range;
        if (!get_f32(r, &x) || !get_f32(r, &y) || !get_f32(r, &z) || !get_f32(r, &intensity) || !get_f32(r, &range))
            return 0;
        MxLight *li = (MxLight *)mx_comp_add(w, e, MX_COMP_LIGHT);
        if (!li) return 0;
        li->color = mx_v3(x, y, z);
        li->intensity = intensity;
        li->range = range;
    }
    if (mask & ((uint32_t)1 << MX_COMP_SPRITE)) {
        uint32_t kind, texture, color, layer;
        float rect[4];
        char text[1024], image[1024];
        if (!get_u32(r, &kind) || !get_u32(r, &texture) || !get_u32(r, &color) || !get_u32(r, &layer)) return 0;
        for (int i = 0; i < 4; i++)
            if (!get_f32(r, &rect[i])) return 0;
        if (!get_str(r, text, sizeof(text)) || !get_str(r, image, sizeof(image))) return 0;
        MxSprite *sp = (MxSprite *)mx_comp_add(w, e, MX_COMP_SPRITE);
        if (!sp) return 0;
        sp->kind = (int)kind;
        sp->texture = (int)texture;
        sp->color = color;
        sp->layer = (int)layer;
        for (int i = 0; i < 4; i++) sp->rect[i] = rect[i];
        sp->text = text[0] ? mx_str_intern(text) : NULL;
        sp->image = image[0] ? mx_str_intern(image) : NULL;
    }
    return !r->bad;
}

int mx_save_write(const MxWorld *w, const char *slot)
{
    char path[1200];
    if (!w || mx_save_path(path, (int)sizeof(path), slot) != 0) return -1;
    Buf b;
    memset(&b, 0, sizeof(b));
    put_bytes(&b, "MXSV", 4);
    put_u32(&b, SAVE_VERSION);
    put_u32(&b, 0);
    uint32_t count = (uint32_t)mx_entity_count(w);
    put_u32(&b, count);
    for (int i = 0; i < mx_entity_capacity(w); i++) {
        MxEntity e = mx_world_entity_at(w, i);
        if (!mx_entity_alive(w, e)) continue;
        const MxTransform *t = (const MxTransform *)mx_comp_get(w, e, MX_COMP_TRANSFORM);
        put_u32(&b, e.index);
        put_str(&b, mx_entity_name(w, e));
        put_str(&b, mx_entity_type(w, e));
        put_f32(&b, t ? t->position.x : 0.0f);
        put_f32(&b, t ? t->position.y : 0.0f);
        put_f32(&b, t ? t->position.z : 0.0f);
        put_f32(&b, t ? t->rotation.x : 0.0f);
        put_f32(&b, t ? t->rotation.y : 0.0f);
        put_f32(&b, t ? t->rotation.z : 0.0f);
        put_f32(&b, t ? t->rotation.w : 1.0f);
        put_f32(&b, t ? t->scale.x : 1.0f);
        put_f32(&b, t ? t->scale.y : 1.0f);
        put_f32(&b, t ? t->scale.z : 1.0f);
        MxEntity parent = mx_entity_parent(w, e);
        put_u32(&b, mx_entity_alive(w, parent) ? parent.index : 0xFFFFFFFFu);
        put_u32(&b, component_mask(w, e));
        write_components(w, e, &b);
    }
    uint32_t crc = crc32(b.data + 12, b.len - 12);
    put_u32(&b, crc);
    int r = b.oom ? -1 : plat_write_file_atomic(path, b.data, (int)b.len);
    mx_mem_free(b.data);
    return r;
}

int mx_save_read(MxWorld *w, const char *slot)
{
    char path[1200];
    if (!w || mx_save_path(path, (int)sizeof(path), slot) != 0) return -1;
    int size = 0;
    unsigned char *data = (unsigned char *)plat_read_file(path, &size, 0);
    if (!data) return -1;
    int ok = 0;
    do {
        if (size < 20 || memcmp(data, "MXSV", 4) != 0) break;
        Rd head = {data + 4, (size_t)size - 4, 0, 0};
        uint32_t version = 0, flags = 0;
        if (!get_u32(&head, &version) || !get_u32(&head, &flags)) break;
        if (version != SAVE_VERSION) {
            mx_set_error("save: unsupported version %u", version);
            break;
        }
        size_t payload_len = (size_t)size - 16;
        uint32_t stored;
        stored = (uint32_t)data[size - 4] | (uint32_t)data[size - 3] << 8 | (uint32_t)data[size - 2] << 16 |
                 (uint32_t)data[size - 1] << 24;
        if (crc32(data + 12, payload_len) != stored) {
            mx_set_error("save: checksum mismatch");
            break;
        }
        Rd r = {data + 12, payload_len, 0, 0};
        uint32_t count = 0;
        if (!get_u32(&r, &count) || count > SAVE_MAX_ENTITIES) break;
        MxWorld *tmp = mx_world_create();
        MxEntity *created = (MxEntity *)mx_mem_alloc(sizeof(MxEntity) * (count ? count : 1));
        uint32_t *parent_old = (uint32_t *)mx_mem_alloc(sizeof(uint32_t) * (count ? count : 1));
        MxMap *index_map = mx_map_new();
        int good = tmp && created && parent_old && index_map;
        for (uint32_t i = 0; good && i < count; i++) {
            uint32_t index, parent, mask;
            char name[1024], type[256];
            float pos[3], rot[4], scale[3];
            if (!get_u32(&r, &index) || !get_str(&r, name, sizeof(name)) || !get_str(&r, type, sizeof(type))) {
                good = 0;
                break;
            }
            for (int k = 0; k < 3; k++)
                if (!get_f32(&r, &pos[k])) good = 0;
            for (int k = 0; k < 4; k++)
                if (!get_f32(&r, &rot[k])) good = 0;
            for (int k = 0; k < 3; k++)
                if (!get_f32(&r, &scale[k])) good = 0;
            if (!get_u32(&r, &parent) || !get_u32(&r, &mask)) good = 0;
            if (!good) break;
            MxEntity e = mx_entity_create(tmp);
            if (!mx_entity_alive(tmp, e)) {
                good = 0;
                break;
            }
            if (name[0]) mx_entity_set_name(tmp, e, name);
            if (type[0]) mx_entity_set_type(tmp, e, type);
            MxTransform *t = (MxTransform *)mx_comp_get(tmp, e, MX_COMP_TRANSFORM);
            if (t) {
                t->position = mx_v3(pos[0], pos[1], pos[2]);
                t->rotation = (MxQuat){rot[0], rot[1], rot[2], rot[3]};
                t->scale = mx_v3(scale[0], scale[1], scale[2]);
                t->prev_position = t->position;
                t->prev_rotation = t->rotation;
                t->prev_scale = t->scale;
            }
            if (!read_components(tmp, e, mask, &r)) {
                good = 0;
                break;
            }
            created[i] = e;
            parent_old[i] = parent;
            mx_map_put(index_map, index, (void *)(uintptr_t)(i + 1));
        }
        if (good && r.at <= r.len) {
            for (uint32_t i = 0; i < count; i++) {
                if (parent_old[i] == 0xFFFFFFFFu) continue;
                int slot = (int)(uintptr_t)mx_map_get(index_map, parent_old[i]) - 1;
                if (slot >= 0 && (uint32_t)slot < count) mx_entity_set_parent(tmp, created[i], created[slot]);
            }
            mx_world_update_transforms(tmp);
            mx_world_swap(w, tmp);
            ok = 1;
        } else {
            mx_set_error("save: truncated or malformed data");
        }
        mx_map_free(index_map);
        mx_mem_free(created);
        mx_mem_free(parent_old);
        mx_world_destroy(tmp);
    } while (0);
    mx_mem_free(data);
    return ok ? 0 : -1;
}

int mx_save_delete(const char *slot)
{
    char path[1200];
    if (mx_save_path(path, (int)sizeof(path), slot) != 0) return -1;
    return plat_remove_file(path);
}

int mx_save_exists(const char *slot)
{
    char path[1200];
    if (mx_save_path(path, (int)sizeof(path), slot) != 0) return 0;
    return plat_file_exists(path);
}
