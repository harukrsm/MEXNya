// Debug text in the engine font (load it with font_load first). Draws immediately.
#ifndef MC_RENDER_TEXT_H
#define MC_RENDER_TEXT_H

#define TEXT_CHAR_W 6
#define TEXT_LINE_H 18

int text_init(void);

// Queue text at screen pixel (x, y); colour is 0xRRGGBB. A dark shadow is added.
void text_print(int x, int y, unsigned int rgb, const char *s);
void text_printf(int x, int y, unsigned int rgb, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

// Kept for compatibility; text is drawn immediately.
void text_flush(void);

#endif
