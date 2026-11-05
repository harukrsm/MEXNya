#include "engine/world/instantiate.h"
#include "engine/world/components.h"
#include "engine/core/math.h"
#include "engine/core/str.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_byte(const char *s)
{
    int hi = hex_digit(s[0]), lo = hex_digit(s[1]);
    return hi < 0 || lo < 0 ? -1 : hi * 16 + lo;
}

static uint32_t parse_color(const char *text, uint32_t fallback)
{
    if (!text || text[0] != '#' || strlen(text) < 7) return fallback;
    int r = hex_byte(text + 1), g = hex_byte(text + 3), b = hex_byte(text + 5);
    if (r < 0 || g < 0 || b < 0) return fallback;
    return 0xFF000000u | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r;
}

static void color_text(char *out, size_t size, uint32_t abgr)
{
    snprintf(out, size, "#%02X%02X%02X", (unsigned)(abgr & 0xFFu), (unsigned)((abgr >> 8) & 0xFFu),
             (unsigned)((abgr >> 16) & 0xFFu));
}

static MxQuat euler_to_quat(const float deg[3])
{
    MxQuat qx = mx_quat_axis_angle(mx_v3(1.0f, 0.0f, 0.0f), deg[0] * MX_DEG2RAD);
    MxQuat qy = mx_quat_axis_angle(mx_v3(0.0f, 1.0f, 0.0f), deg[1] * MX_DEG2RAD);
    MxQuat qz = mx_quat_axis_angle(mx_v3(0.0f, 0.0f, 1.0f), deg[2] * MX_DEG2RAD);
    return mx_quat_mul(mx_quat_mul(qy, qx), qz);
}

static void quat_to_euler(MxQuat q, float deg[3])
{
    MxMat4 m;
    mx_quat_to_mat4(&q, &m);
    float sx = -m.m[9];
    if (sx > 1.0f) sx = 1.0f;
    if (sx < -1.0f) sx = -1.0f;
    deg[0] = asinf(sx) / MX_DEG2RAD;
    deg[1] = atan2f(m.m[8], m.m[10]) / MX_DEG2RAD;
    deg[2] = atan2f(m.m[1], m.m[5]) / MX_DEG2RAD;
}

static int mesh_kind(const char *type)
{
    if (!strcmp(type, "box")) return MX_MESH_BOX;
    if (!strcmp(type, "plane")) return MX_MESH_PLANE;
    if (!strcmp(type, "sphere")) return MX_MESH_SPHERE;
    if (!strcmp(type, "cylinder")) return MX_MESH_CYLINDER;
    if (!strcmp(type, "cone")) return MX_MESH_CONE;
    if (!strcmp(type, "model")) return MX_MESH_MODEL;
    return -1;
}

static int sprite_kind(const char *type)
{
    if (!strcmp(type, "rect")) return MX_SPRITE_RECT;
    if (!strcmp(type, "panel")) return MX_SPRITE_PANEL;
    if (!strcmp(type, "button")) return MX_SPRITE_BUTTON;
    if (!strcmp(type, "text")) return MX_SPRITE_TEXT;
    if (!strcmp(type, "image")) return MX_SPRITE_IMAGE;
    return -1;
}

static void fill_object(MxWorld *w, MxEntity e, const Json *o)
{
    const char *type = json_gets(o, "type", "box");
    const char *id = json_gets(o, "id", json_gets(o, "name", NULL));
    if (id && id[0]) mx_entity_set_name(w, e, id);
    mx_entity_set_type(w, e, type);

    MxTransform *t = (MxTransform *)mx_comp_get(w, e, MX_COMP_TRANSFORM);
    if (t) {
        float pos[3] = {0.0f, 0.0f, 0.0f}, rot[3] = {0.0f, 0.0f, 0.0f}, scale[3] = {1.0f, 1.0f, 1.0f};
        json_getv(o, "pos", pos, 3);
        json_getv(o, "rot", rot, 3);
        json_getv(o, "scale", scale, 3);
        t->position = mx_v3(pos[0], pos[1], pos[2]);
        t->scale = mx_v3(scale[0], scale[1], scale[2]);
        t->rotation = euler_to_quat(rot);
        t->prev_position = t->position;
        t->prev_scale = t->scale;
        t->prev_rotation = t->rotation;
    }

    int mesh = mesh_kind(type);
    if (mesh >= 0) {
        MxMeshRenderer *m = (MxMeshRenderer *)mx_comp_add(w, e, MX_COMP_MESH);
        if (m) {
            m->mesh = mesh;
            m->tint = parse_color(json_gets(o, "color", NULL), 0xFFB8B8B4u);
            m->layer = 0;
            if (mesh == MX_MESH_MODEL) {
                const char *path = json_gets(o, "model", NULL);
                m->asset = path && path[0] ? mx_str_intern(path) : NULL;
            }
        }
    } else if (!strcmp(type, "light")) {
        MxLight *l = (MxLight *)mx_comp_add(w, e, MX_COMP_LIGHT);
        if (l) {
            uint32_t c = parse_color(json_gets(o, "color", NULL), 0xFFFFF2D8u);
            l->color = mx_v3((float)(c & 0xFFu) / 255.0f, (float)((c >> 8) & 0xFFu) / 255.0f,
                             (float)((c >> 16) & 0xFFu) / 255.0f);
            l->intensity = 1.0f;
            l->range = 0.0f;
        }
    } else {
        int kind = sprite_kind(type);
        if (kind >= 0) {
            MxSprite *s = (MxSprite *)mx_comp_add(w, e, MX_COMP_SPRITE);
            if (s) {
                float rect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                json_getv(o, "rect", rect, 4);
                memcpy(s->rect, rect, sizeof(rect));
                s->kind = kind;
                s->color = parse_color(json_gets(o, "color", NULL), 0xFF3A3D42u);
                const char *text = json_gets(o, "text", NULL);
                const char *image = json_gets(o, "image", NULL);
                s->text = text && text[0] ? mx_str_intern(text) : NULL;
                s->image = image && image[0] ? mx_str_intern(image) : NULL;
            }
        }
    }
}

MxWorld *mx_world_from_scene(const Json *scene)
{
    MxWorld *w = mx_world_create();
    if (!w) return NULL;
    const Json *objects = json_get(scene, "objects");
    for (const Json *o = objects ? objects->child : NULL; o; o = o->next) {
        MxEntity e = mx_entity_create(w);
        if (!mx_entity_alive(w, e)) {
            mx_world_destroy(w);
            return NULL;
        }
        fill_object(w, e, o);
    }
    mx_world_update_transforms(w);
    return w;
}

Json *mx_world_to_json(const MxWorld *w)
{
    Json *doc = json_obj();
    Json *objects = json_set(doc, "objects", json_arr());
    if (!objects) {
        json_free(doc);
        return NULL;
    }
    for (int i = 0; i < mx_entity_capacity(w); i++) {
        MxEntity e = mx_world_entity_at(w, i);
        if (!mx_entity_alive(w, e)) continue;
        Json *o = json_obj();
        const char *name = mx_entity_name(w, e);
        const char *type = mx_entity_type(w, e);
        if (name[0]) json_sets(o, "id", name);
        json_sets(o, "type", type[0] ? type : "box");
        const MxTransform *t = (const MxTransform *)mx_comp_get(w, e, MX_COMP_TRANSFORM);
        if (t) {
            float pos[3] = {t->position.x, t->position.y, t->position.z};
            float rot[3];
            float scale[3] = {t->scale.x, t->scale.y, t->scale.z};
            quat_to_euler(t->rotation, rot);
            json_setv(o, "pos", pos, 3);
            json_setv(o, "rot", rot, 3);
            json_setv(o, "scale", scale, 3);
        }
        const MxMeshRenderer *m = (const MxMeshRenderer *)mx_comp_get(w, e, MX_COMP_MESH);
        if (m) {
            char color[8];
            color_text(color, sizeof(color), m->tint);
            json_sets(o, "color", color);
            if (m->mesh == MX_MESH_MODEL && m->asset) json_sets(o, "model", m->asset);
        }
        const MxLight *l = (const MxLight *)mx_comp_get(w, e, MX_COMP_LIGHT);
        if (l) {
            char color[8];
            unsigned r = (unsigned)(l->color.x * 255.0f + 0.5f);
            unsigned g = (unsigned)(l->color.y * 255.0f + 0.5f);
            unsigned b = (unsigned)(l->color.z * 255.0f + 0.5f);
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            snprintf(color, sizeof(color), "#%02X%02X%02X", r, g, b);
            json_sets(o, "color", color);
        }
        const MxSprite *s = (const MxSprite *)mx_comp_get(w, e, MX_COMP_SPRITE);
        if (s) {
            char color[8];
            color_text(color, sizeof(color), s->color);
            json_setv(o, "rect", s->rect, 4);
            json_sets(o, "color", color);
            if (s->text) json_sets(o, "text", s->text);
            if (s->image) json_sets(o, "image", s->image);
        }
        json_push(objects, o);
    }
    return doc;
}
