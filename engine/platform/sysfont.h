// The operating system's font engine (CoreText on macOS, GDI on Windows), for smooth
// UI text at any pixel density. Elsewhere sysfont_open fails and callers keep the
// bundled pixel font.
#ifndef MC_PLATFORM_SYSFONT_H
#define MC_PLATFORM_SYSFONT_H

// Opens the UI face suited to a language ("en", "zh-Hans", "zh-Hant", "ja", "ko") at
// px pixels. Returns 0 on success.
int sysfont_open(const char *lang, float px);
void sysfont_close(void);
// Ascent and descent in pixels of the open face.
void sysfont_metrics(float *ascent, float *descent);
// Renders one character into an 8-bit coverage image of w x h (w = cell width
// returned in *out_w, h = the cell height given). The baseline is at `baseline` pixels
// from the top. Characters the face lacks come from the system's fallback fonts.
// Returns the advance in pixels, or -1 when nothing could be drawn.
float sysfont_glyph(unsigned int cp, unsigned char *coverage, int max_w, int h, int baseline, int *out_w);

#endif
