// Debug-only: dump a 16-bit (RGB565) framebuffer to a 24-bit BMP file.
#include "screenshot.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

static void put_u32(unsigned char *p, unsigned int v)
{
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

int screenshot_save_565(const char *path, const unsigned short *pixels, int width, int height, int stride)
{
    if (width <= 0 || height <= 0 || width > 8192)
        return -1;
    int row_bytes = (width * 3 + 3) & ~3;
    unsigned char *buf = malloc(54 + (size_t)row_bytes * height);
    if (!buf)
        return -2;
    memset(buf, 0, 54);
    buf[0] = 'B'; buf[1] = 'M';
    put_u32(buf + 2, 54 + (unsigned)row_bytes * (unsigned)height);
    put_u32(buf + 10, 54);
    put_u32(buf + 14, 40);
    put_u32(buf + 18, (unsigned)width);
    put_u32(buf + 22, (unsigned)height);
    buf[26] = 1;
    buf[28] = 24;
    for (int y = 0; y < height; y++) {
        unsigned char *row = buf + 54 + (size_t)y * row_bytes;
        const unsigned short *src = pixels + (size_t)(height - 1 - y) * stride;  // BMP rows bottom-up
        for (int x = 0; x < width; x++) {
            unsigned short c = src[x];
            unsigned int r = c & 0x1F, g = (c >> 5) & 0x3F, b = (c >> 11) & 0x1F;
            row[x * 3 + 0] = (unsigned char)((b << 3) | (b >> 2));
            row[x * 3 + 1] = (unsigned char)((g << 2) | (g >> 4));
            row[x * 3 + 2] = (unsigned char)((r << 3) | (r >> 2));
        }
    }
    int ok = plat_write_file_atomic(path, buf, 54 + row_bytes * height);
    free(buf);
    return ok;
}
