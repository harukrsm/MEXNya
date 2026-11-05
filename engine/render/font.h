// UTF-8 text from one of two sources:
//   FONT_PIXEL   the bundled pixel font (engine/resources/fonts/mx12.mxf, baked by
//                engine/tools/gen_font.py): Latin, simplified and traditional Chinese,
//                Japanese and Korean. Works everywhere, sharp at whole UI scales.
//   FONT_SYSTEM  the OS font engine (macOS, Windows), smooth at the screen's own pixel
//                density, with the OS fallback fonts for anything else.
// Sizes are in UI points either way (cell 16, line 18), so layout never depends on the
// source. Each call draws at once, so text stacks correctly with other 2D drawing.
#ifndef MC_RENDER_FONT_H
#define MC_RENDER_FONT_H

// Loads a .mxf file (absolute path). Returns 0 on success.
int font_load(const char *path);
enum { FONT_PIXEL = 0, FONT_SYSTEM = 1 };
// Picks the source; lang chooses the system face (CJK glyph forms differ per language);
// pixels_per_point is the UI scale. Returns the source in use (FONT_PIXEL when the
// system font is not available on this platform).
int font_set_mode(int mode, const char *lang, int pixels_per_point);
int font_mode(void);
int font_ready(void);
void font_free(void);

int font_line_h(void);      // line advance in points
int font_cell_h(void);      // glyph cell height in points
int font_width(const char *utf8);
int font_width_n(const char *utf8, int bytes);
// Byte offset of the character nearest to pixel x (caret placement).
int font_index_at(const char *utf8, int x);
// Bytes of utf8 that fit in max_w pixels.
int font_fit(const char *utf8, int max_w);

// Colour 0xRRGGBB. Draws immediately in the 2D pass. Returns the width drawn.
int font_draw(int x, int y, unsigned int rgb, const char *utf8);
int font_draw_n(int x, int y, unsigned int rgb, const char *utf8, int bytes);
// Whole-number magnification (canvas zoom): each font pixel becomes scale x scale.
int font_draw_scaled(int x, int y, unsigned int rgb, const char *utf8, int bytes, int scale);
int font_drawf(int x, int y, unsigned int rgb, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

// Decodes one UTF-8 character; returns its length in bytes (>= 1).
int utf8_next(const char *s, unsigned int *cp);
// Byte length of the character that ends right before s + at.
int utf8_prev_len(const char *s, int at);

#endif
