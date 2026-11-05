// Debug-only: dump a 16-bit (RGB565) framebuffer to a 24-bit BMP file.
#ifndef MC_PLATFORM_SCREENSHOT_H
#define MC_PLATFORM_SCREENSHOT_H

// `pixels` points at the first pixel of the buffer, `stride` in pixels.
int screenshot_save_565(const char *path, const unsigned short *pixels, int width, int height, int stride);

#endif
