// 2D drawing helpers for the 480x272 UI layer (call after gfx_begin_2d).
#ifndef MC_RENDER_UI_H
#define MC_RENDER_UI_H

// Colours are 0xRRGGBB; alpha 0..255.
void ui_rect(int x, int y, int w, int h, unsigned int rgb, int alpha);
void ui_frame(int x, int y, int w, int h, unsigned int rgb);

// A filled triangle (screen pixels, floats).
void ui_tri(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int rgb);

// Adds `rgb` to every pixel of the screen (lifts the blacks).
void ui_lift(unsigned int rgb);

// Restrict drawing to a rectangle; ui_clip_reset() goes back to the full screen.
void ui_clip(int x, int y, int w, int h);
void ui_clip_reset(void);

#endif
