// PNG writer (fixed-Huffman deflate with LZ77), so tool snapshots are small and any
// image viewer or AI agent can open them.
#ifndef MC_CORE_PNG_H
#define MC_CORE_PNG_H

// rgb: 3 bytes per pixel, rows of w pixels. Returns 0 on success.
int png_save_rgb(const char *path, const unsigned char *rgb, int w, int h);
// RGB565 as the renderers produce it (red in the low bits), stride in pixels.
int png_save_565(const char *path, const unsigned short *pixels, int w, int h, int stride);

#endif
