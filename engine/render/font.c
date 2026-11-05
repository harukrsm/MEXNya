#include "font.h"
#include "render_api.h"
#include "engine/platform/platform.h"
#include "engine/platform/sysfont.h"
#include <math.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned int cp;
    unsigned char adv, width;
    unsigned short reserved;
} Glyph;

static Glyph *s_glyphs;
static int s_count, s_cell_w, s_cell_h, s_line_h, s_cols;
static unsigned char *s_atlas;   // T4, low nibble = left pixel
static int s_atlas_w, s_atlas_h;
static uint32_t s_clut[16];
static RTexDesc s_tex;
#define BATCH 512
static int s_mode;  // FONT_PIXEL or FONT_SYSTEM
static RVertex s_verts[BATCH * 2];

static unsigned int rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24; }
static unsigned int rd16(const unsigned char *p) { return p[0] | p[1] << 8; }

void font_free(void)
{
    free(s_glyphs); s_glyphs = NULL;
    free(s_atlas); s_atlas = NULL;
    s_count = 0;
}

int font_load(const char *path)
{
    int size = 0;
    unsigned char *f = (unsigned char *)plat_read_file(path, &size, 0);
    if (!f) return -1;
    if (size < 16 || memcmp(f, "MXF1", 4) != 0) { free(f); return -2; }
    int count = (int)rd32(f + 4);
    int cw = (int)rd16(f + 8), ch = (int)rd16(f + 10), lh = (int)rd16(f + 12);
    int need = 16 + count * 8 + count * ch * 2;
    if (count <= 0 || cw > 16 || ch <= 0 || size < need) { free(f); return -3; }
    font_free();
    s_glyphs = (Glyph *)malloc(sizeof(Glyph) * (size_t)count);
    s_cols = 1024 / cw;
    s_atlas_w = 1024;
    s_atlas_h = ((count + s_cols - 1) / s_cols) * ch;
    s_atlas = (unsigned char *)calloc((size_t)(s_atlas_w / 2) * (size_t)s_atlas_h, 1);
    if (!s_glyphs || !s_atlas) { free(f); font_free(); return -4; }
    const unsigned char *e = f + 16, *bm = f + 16 + count * 8;
    for (int i = 0; i < count; i++, e += 8) {
        s_glyphs[i].cp = rd32(e);
        s_glyphs[i].adv = e[4];
        s_glyphs[i].width = e[5];
        int ox = (i % s_cols) * cw, oy = (i / s_cols) * ch;
        for (int y = 0; y < ch; y++) {
            const unsigned char *row = bm + ((size_t)i * ch + y) * 2;
            unsigned int bits = (unsigned int)row[0] << 8 | row[1];
            for (int x = 0; x < cw; x++) {
                if (!(bits & (0x8000u >> x))) continue;
                int ax = ox + x;
                unsigned char *p = s_atlas + (size_t)(oy + y) * (s_atlas_w / 2) + ax / 2;
                *p |= (ax & 1) ? 0x10 : 0x01;
            }
        }
    }
    free(f);
    s_count = count;
    s_cell_w = cw;
    s_cell_h = ch;
    s_line_h = lh;
    s_clut[0] = 0x00000000u;
    s_clut[1] = 0xFFFFFFFFu;
    memset(&s_tex, 0, sizeof(s_tex));
    s_tex.w = s_atlas_w;
    s_tex.h = s_atlas_h;
    s_tex.format = R_TEX_T4;
    s_tex.alpha0 = 1;
    s_tex.pixels = s_atlas;
    s_tex.clut = s_clut;
    s_tex.wrap = R_WRAP_CLAMP;
    return 0;
}

int font_ready(void) { return s_count > 0 || s_mode == FONT_SYSTEM; }
int font_line_h(void) { return s_mode == FONT_SYSTEM ? 18 : (s_count ? s_line_h : 9); }
int font_cell_h(void) { return s_mode == FONT_SYSTEM ? 16 : (s_count ? s_cell_h : 8); }

int utf8_next(const char *s, unsigned int *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) { *cp = (u[0] & 0x1Fu) << 6 | (u[1] & 0x3Fu); return 2; }
    if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *cp = (u[0] & 0x0Fu) << 12 | (u[1] & 0x3Fu) << 6 | (u[2] & 0x3Fu);
        return 3;
    }
    if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
        *cp = (u[0] & 0x07u) << 18 | (u[1] & 0x3Fu) << 12 | (u[2] & 0x3Fu) << 6 | (u[3] & 0x3Fu);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

int utf8_prev_len(const char *s, int at)
{
    int n = 1;
    while (at - n > 0 && ((unsigned char)s[at - n] & 0xC0) == 0x80 && n < 4) n++;
    return at > 0 ? n : 0;
}

static int find(unsigned int cp)
{
    int lo = 0, hi = s_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s_glyphs[mid].cp == cp) return mid;
        if (s_glyphs[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

// ---------------------------------------------------------------- system font
// Glyphs come from the OS font engine at the screen's own resolution (points x scale)
// and are cached in a 16-level coverage atlas; the cache refills when it runs out.
#define SYS_W 1024
#define SYS_H 2048
#define SYS_SLOTS 8192
typedef struct { unsigned int cp; short x, y, w; float adv; } SysGlyph;
static int s_sys_scale = 1;        // pixels per point
static int s_sys_cell, s_sys_base; // cell height and baseline in pixels
static unsigned char *s_sys_atlas;
static SysGlyph s_sys[SYS_SLOTS];
static int s_sys_n, s_sys_x, s_sys_y, s_sys_dirty, s_sys_epoch;
static uint32_t s_sys_clut[16];
static RTexDesc s_sys_tex;
static unsigned char s_cov[256 * 256];

static void sys_reset(void)
{
    memset(s_sys, 0, sizeof(s_sys));
    if (s_sys_atlas) memset(s_sys_atlas, 0, (size_t)SYS_W / 2 * SYS_H);
    s_sys_n = 0;
    s_sys_x = s_sys_y = 0;
    s_sys_dirty = 1;
    s_sys_epoch++;
}

int font_set_mode(int mode, const char *lang, int pixels_per_point)
{
    if (pixels_per_point < 1) pixels_per_point = 1;
    if (mode == FONT_SYSTEM) {
        float px = 12.5f * (float)pixels_per_point;
        if (sysfont_open(lang ? lang : "en", px) != 0) { s_mode = FONT_PIXEL; return s_mode; }
        if (!s_sys_atlas) s_sys_atlas = (unsigned char *)calloc((size_t)SYS_W / 2 * SYS_H, 1);
        if (!s_sys_atlas) { sysfont_close(); s_mode = FONT_PIXEL; return s_mode; }
        float asc = 0, desc = 0;
        sysfont_metrics(&asc, &desc);
        s_sys_scale = pixels_per_point;
        s_sys_cell = 16 * pixels_per_point;
        int top = (int)((s_sys_cell - (asc + desc)) * 0.5f + 0.5f);
        s_sys_base = top + (int)(asc + 0.5f);
        for (int i = 0; i < 16; i++) s_sys_clut[i] = (uint32_t)(i * 17) << 24 | 0x00FFFFFFu;
        memset(&s_sys_tex, 0, sizeof(s_sys_tex));
        s_sys_tex.w = SYS_W;
        s_sys_tex.h = SYS_H;
        s_sys_tex.format = R_TEX_T4;
        s_sys_tex.alpha0 = 1;
        s_sys_tex.pixels = s_sys_atlas;
        s_sys_tex.clut = s_sys_clut;
        s_sys_tex.wrap = R_WRAP_CLAMP;
        sys_reset();
        s_mode = FONT_SYSTEM;
        return s_mode;
    }
    sysfont_close();
    s_mode = FONT_PIXEL;
    return s_mode;
}

int font_mode(void) { return s_mode; }

// The cached glyph for cp (rasterised on first use), or NULL when the atlas is full.
static SysGlyph *sys_glyph(unsigned int cp)
{
    unsigned int h = (cp * 2654435761u) & (SYS_SLOTS - 1);
    for (int probe = 0; probe < SYS_SLOTS; probe++, h = (h + 1) & (SYS_SLOTS - 1)) {
        SysGlyph *g = &s_sys[h];
        if (g->w && g->cp == cp) return g;
        if (g->w) continue;
        if (s_sys_n > SYS_SLOTS * 3 / 4) return NULL;
        int w = 0;
        int cell = s_sys_cell <= 256 ? s_sys_cell : 256;
        float adv = sysfont_glyph(cp, s_cov, 256, cell, s_sys_base, &w);
        if (adv < 0) {
            // Not drawable: keep an empty, space-like entry so it is not retried.
            adv = (float)s_sys_cell * 0.5f;
            w = 1;
            memset(s_cov, 0, (size_t)cell);
        }
        if (s_sys_x + w > SYS_W) { s_sys_x = 0; s_sys_y += cell; }
        if (s_sys_y + cell > SYS_H) return NULL;
        for (int y = 0; y < cell; y++)
            for (int x = 0; x < w; x++) {
                int v = s_cov[y * w + x] >> 4;
                if (!v) continue;
                int ax = s_sys_x + x;
                unsigned char *px = s_sys_atlas + (size_t)(s_sys_y + y) * (SYS_W / 2) + ax / 2;
                *px |= (ax & 1) ? (unsigned char)(v << 4) : (unsigned char)v;
            }
        g->cp = cp;
        g->x = (short)s_sys_x;
        g->y = (short)s_sys_y;
        g->w = (short)w;
        g->adv = adv / (float)s_sys_scale;
        s_sys_x += w + 1;
        s_sys_n++;
        s_sys_dirty = 1;
        return g;
    }
    return NULL;
}

// Advance of one character in points.
static float advance_f(unsigned int cp)
{
    if (s_mode == FONT_SYSTEM) {
        SysGlyph *g = sys_glyph(cp);
        if (!g) { sys_reset(); g = sys_glyph(cp); }
        return g ? g->adv : 6.0f;
    }
    if (!s_count) return 6;
    int g = find(cp);
    if (g < 0) g = find('?');
    if (cp >= 0x2E80 && g >= 0 && s_glyphs[g].adv < 12) return 12;
    return g >= 0 ? s_glyphs[g].adv : s_cell_w / 2;
}

int font_width_n(const char *s, int bytes)
{
    float w = 0;
    for (int i = 0; i < bytes && s[i];) {
        unsigned int cp;
        i += utf8_next(s + i, &cp);
        if (cp == '\n') break;
        w += advance_f(cp);
    }
    return (int)ceilf(w);
}

int font_width(const char *s) { return s ? font_width_n(s, (int)strlen(s)) : 0; }

int font_index_at(const char *s, int x)
{
    float w = 0;
    int i = 0;
    while (s[i]) {
        unsigned int cp;
        int n = utf8_next(s + i, &cp);
        float a = advance_f(cp);
        if (x < w + a / 2) return i;
        w += a;
        i += n;
    }
    return i;
}

int font_fit(const char *s, int max_w)
{
    float w = 0;
    int i = 0;
    while (s[i]) {
        unsigned int cp;
        int n = utf8_next(s + i, &cp);
        float a = advance_f(cp);
        if (w + a > max_w) break;
        w += a;
        i += n;
    }
    return i;
}

static unsigned int abgr(unsigned int rgb)
{
    return 0xFF000000u | (rgb & 0xFF) << 16 | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

int font_draw_n(int x, int y, unsigned int rgb, const char *s, int bytes)
{
    return font_draw_scaled(x, y, rgb, s, bytes, 1);
}

static int sys_draw(int x, int y, unsigned int c, const char *s, int bytes, int scale)
{
    // Make sure every glyph of the string is in the atlas before drawing any of it, so a
    // refill can never pull a glyph out from under a queued quad.
    int epoch = s_sys_epoch;
    for (int pass = 0; pass < 2; pass++) {
        int full = 0;
        for (int i = 0; i < bytes && s[i];) {
            unsigned int cp;
            i += utf8_next(s + i, &cp);
            if (cp == '\n') break;
            if (!sys_glyph(cp)) { full = 1; break; }
        }
        if (!full) break;
        sys_reset();
    }
    (void)epoch;
    float k = (float)s_sys_scale, tw = (float)SYS_W, th = (float)SYS_H;
    float pen = (float)x;
    int n = 0;
    s_sys_tex.dynamic = s_sys_dirty;
    r_bind_texture(&s_sys_tex);
    s_sys_tex.dynamic = 0;
    s_sys_dirty = 0;
    r_set_blend(R_BLEND_ALPHA);
    r_set_alpha_test(1);
    float cell = (float)s_sys_cell / k * (float)scale;
    for (int i = 0; i < bytes && s[i];) {
        unsigned int cp;
        i += utf8_next(s + i, &cp);
        if (cp == '\n') break;
        SysGlyph *g = sys_glyph(cp);
        if (!g) break;
        if (cp != ' ' && g->w > 1) {
            if (n == BATCH) { r_draw(R_PRIM_SPRITES, s_verts, n * 2); n = 0; }
            float px = roundf(pen * k) / k;  // whole screen pixels keep the glyph sharp
            float gw = (float)g->w / k * (float)scale;
            int cellpx = s_sys_cell <= 256 ? s_sys_cell : 256;
            s_verts[n * 2] = (RVertex){px, (float)y, 0, g->x / tw, g->y / th, c};
            s_verts[n * 2 + 1] = (RVertex){px + gw, (float)y + cell, 0, (g->x + g->w) / tw, (g->y + cellpx) / th, c};
            n++;
        }
        pen += g->adv * (float)scale;
    }
    if (n) r_draw(R_PRIM_SPRITES, s_verts, n * 2);
    r_set_alpha_test(0);
    r_set_blend(R_BLEND_NONE);
    r_bind_texture(NULL);
    return (int)ceilf(pen - (float)x);
}

int font_draw_scaled(int x, int y, unsigned int rgb, const char *s, int bytes, int scale)
{
    if (!s) return 0;
    if (scale < 1) scale = 1;
    if (bytes < 0) bytes = (int)strlen(s);
    if (s_mode == FONT_SYSTEM) return sys_draw(x, y, abgr(rgb), s, bytes, scale);
    int x0 = x, n = 0;
    unsigned int c = abgr(rgb);
    float tw = (float)s_atlas_w, th = (float)s_atlas_h;
    if (!s_count) return 0;
    r_bind_texture(&s_tex);
    r_set_alpha_test(1);
    for (int i = 0; i < bytes && s[i];) {
        unsigned int cp;
        i += utf8_next(s + i, &cp);
        if (cp == '\n') break;
        int g = find(cp);
        if (g < 0) g = find('?');
        int a = (int)advance_f(cp);
        if (g >= 0 && cp != ' ') {
            if (n == BATCH) { r_draw(R_PRIM_SPRITES, s_verts, n * 2); n = 0; }
            int gx = (g % s_cols) * s_cell_w, gy = (g / s_cols) * s_cell_h;
            int gw = s_glyphs[g].width ? s_glyphs[g].width : 1;
            s_verts[n * 2] = (RVertex){(float)x, (float)y, 0, gx / tw, gy / th, c};
            s_verts[n * 2 + 1] = (RVertex){(float)(x + gw * scale), (float)(y + s_cell_h * scale), 0,
                                           (gx + gw) / tw, (gy + s_cell_h) / th, c};
            n++;
        }
        x += a * scale;
    }
    if (n) r_draw(R_PRIM_SPRITES, s_verts, n * 2);
    r_set_alpha_test(0);
    r_bind_texture(NULL);
    return x - x0;
}

int font_draw(int x, int y, unsigned int rgb, const char *s)
{
    return s ? font_draw_n(x, y, rgb, s, (int)strlen(s)) : 0;
}

int font_drawf(int x, int y, unsigned int rgb, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return font_draw(x, y, rgb, buf);
}
