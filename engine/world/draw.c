#include "engine/world/draw.h"
#include "engine/world/components.h"
#include "engine/render/font.h"
#include "engine/render/model.h"
#include "engine/render/primitives.h"
#include "engine/render/render_api.h"
#include "engine/render/ui.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char path[128];
    Model m;
    int ok;
} ModelSlot;

static ModelSlot s_models[64];

static const Model *model_get(const char *path)
{
    if (!path || !*path) return NULL;
    for (int i = 0; i < 64; i++)
        if (s_models[i].path[0] && !strcmp(s_models[i].path, path)) return s_models[i].ok ? &s_models[i].m : NULL;
    for (int i = 0; i < 64; i++)
        if (!s_models[i].path[0]) {
            snprintf(s_models[i].path, sizeof(s_models[i].path), "%s", path);
            s_models[i].ok = model_load(&s_models[i].m, path) == 0;
            if (!s_models[i].ok) fprintf(stderr, "world: model %s not found\n", path);
            return s_models[i].ok ? &s_models[i].m : NULL;
        }
    return NULL;
}

void mx_world_draw_3d(const MxWorld *w)
{
    if (!w) return;
    r_set_cull(0);
    r_set_fog(0);
    for (int i = 0; i < mx_entity_capacity(w); i++) {
        MxEntity e = mx_world_entity_at(w, i);
        if (!mx_entity_alive(w, e)) continue;
        const MxMeshRenderer *m = (const MxMeshRenderer *)mx_comp_get(w, e, MX_COMP_MESH);
        const MxTransform *t = (const MxTransform *)mx_comp_get(w, e, MX_COMP_TRANSFORM);
        if (!m || !t) continue;
        if (m->mesh >= MX_MESH_BOX && m->mesh <= MX_MESH_CONE) {
            r_draw_shape((RShape)m->mesh, t->position, t->rotation, t->scale, m->tint);
        } else if (m->mesh == MX_MESH_MODEL && m->asset) {
            const Model *model = model_get(m->asset);
            if (!model) continue;
            MxTrs xf = {t->position, t->rotation, t->scale};
            MxMat4 matrix;
            mx_transform_compose(&matrix, &xf);
            r_matrix_mode(R_MATRIX_MODEL);
            r_load_matrix(&matrix);
            r_set_cull(1);
            model_draw(model);
            r_set_cull(0);
            r_load_identity();
        }
    }
}

static uint32_t abgr_to_rgb(uint32_t abgr)
{
    return ((abgr & 0xFFu) << 16) | (abgr & 0xFF00u) | ((abgr >> 16) & 0xFFu);
}

void mx_world_draw_2d(const MxWorld *w, int x, int y, float scale)
{
    int ts = (int)scale;
    if (ts < 1) ts = 1;
    if (!w) return;
    for (int i = 0; i < mx_entity_capacity(w); i++) {
        MxEntity e = mx_world_entity_at(w, i);
        if (!mx_entity_alive(w, e)) continue;
        const MxSprite *s = (const MxSprite *)mx_comp_get(w, e, MX_COMP_SPRITE);
        if (!s) continue;
        int ox = x + (int)(s->rect[0] * scale);
        int oy = y + (int)(s->rect[1] * scale);
        int sw = (int)(s->rect[2] * scale);
        int sh = (int)(s->rect[3] * scale);
        unsigned rgb = abgr_to_rgb(s->color);
        const char *text = s->text ? s->text : "";
        if (s->kind == MX_SPRITE_TEXT) {
            font_draw_scaled(ox, oy, rgb, text, -1, ts);
            continue;
        }
        if (s->kind == MX_SPRITE_IMAGE) continue;
        ui_rect(ox, oy, sw, sh, rgb, 255);
        if (s->kind == MX_SPRITE_PANEL || s->kind == MX_SPRITE_BUTTON) ui_frame(ox, oy, sw, sh, 0x7A7F87);
        if (s->kind == MX_SPRITE_BUTTON) {
            int tw = font_width(text) * ts;
            font_draw_scaled(ox + (sw - tw) / 2, oy + (sh - font_cell_h() * ts) / 2, 0xE6E6E6, text, -1, ts);
        }
    }
}
