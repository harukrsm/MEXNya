#include "ui.h"
#include "render_api.h"
#include <stddef.h>
#include "engine/render/render_config.h"

static unsigned int abgr(unsigned int rgb, int alpha)
{
    return ((unsigned int)alpha << 24) | ((rgb & 0xFF) << 16) | ((rgb & 0xFF00) | ((rgb >> 16) & 0xFF));
}

void ui_rect(int x, int y, int w, int h, unsigned int rgb, int alpha)
{
    RVertex v[2];
    unsigned int c = abgr(rgb, alpha);
    v[0] = (RVertex){(float)x, (float)y, 0, 0, 0, c};
    v[1] = (RVertex){(float)(x + w), (float)(y + h), 0, 0, 0, c};
    r_bind_texture(NULL);
    if (alpha < 255)
        r_set_blend(R_BLEND_ALPHA);
    r_draw(R_PRIM_SPRITES, v, 2);
    if (alpha < 255)
        r_set_blend(R_BLEND_NONE);
}

void ui_lift(unsigned int rgb)
{
    RVertex v[2];
    unsigned int c = abgr(rgb, 255);
    v[0] = (RVertex){0, 0, 0, 0, 0, c};
    int w, h;
    r_ui_size(&w, &h);
    v[1] = (RVertex){(float)w, (float)h, 0, 0, 0, c};
    r_bind_texture(NULL);
    r_set_blend(R_BLEND_ADD);
    r_draw(R_PRIM_SPRITES, v, 2);
    r_set_blend(R_BLEND_NONE);
}

void ui_frame(int x, int y, int w, int h, unsigned int rgb)
{
    ui_rect(x, y, w, 1, rgb, 255);
    ui_rect(x, y + h - 1, w, 1, rgb, 255);
    ui_rect(x, y, 1, h, rgb, 255);
    ui_rect(x + w - 1, y, 1, h, rgb, 255);
}

void ui_tri(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int rgb)
{
    RVertex v[3];
    unsigned int c = abgr(rgb, 255);
    v[0] = (RVertex){x0, y0, 0, 0, 0, c};
    v[1] = (RVertex){x1, y1, 0, 0, 0, c};
    v[2] = (RVertex){x2, y2, 0, 0, 0, c};
    r_bind_texture(NULL);
    r_draw(R_PRIM_TRIANGLES, v, 3);
}

void ui_clip(int x, int y, int w, int h) { r_scissor(x, y, w, h); }
void ui_clip_reset(void)
{
    int w, h;
    r_ui_size(&w, &h);
    r_scissor(0, 0, w, h);
}