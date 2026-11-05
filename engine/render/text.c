// Debug text, drawn with the engine font (engine/render/font.h). Kept as a small,
// printf-style API for overlays and tools.
#include "text.h"
#include "font.h"

#include <stdarg.h>
#include <stdio.h>

int text_init(void) { return font_ready() ? 0 : -1; }

void text_print(int x, int y, unsigned int rgb, const char *s)
{
    font_draw(x + 1, y + 1, 0x101214, s);  // shadow
    font_draw(x, y, rgb, s);
}

void text_printf(int x, int y, unsigned int rgb, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    text_print(x, y, rgb, buf);
}

void text_flush(void) {}
