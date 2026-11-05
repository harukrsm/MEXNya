// GU setup, the native-pixel 3D pass, the 2D pass, and frame pacing.
//
// Per frame:
//   gfx_begin_3d();     // 480x272 target; original nearest-sampled pixel textures
//   ...draw world...
//   gfx_begin_2d();     // 2D state, without a resampling/upscale pass
//   ...draw UI...
//   gfx_finish();
//   gfx_present();
#ifndef MC_RENDER_GFX_H
#define MC_RENDER_GFX_H

int gfx_init(void);
void gfx_set_headless(int on);
const char *gfx_backend_name(void);
void gfx_set_backend(const char *name);
int gfx_switch_backend(const char *name);
void gfx_shutdown(void);

// Fog distances in metres; the far clip plane follows fog_far.
void gfx_set_fog(float near_m, float far_m, unsigned int rgb_hex);
float gfx_fog_far(void);

// Sets the GUM projection for the 3D pass.
void gfx_set_projection(float fov_deg);

// Moves the viewport/scissor for a small 3D preview inside the 2D pass; call
// gfx_viewport_screen() afterwards so later 2D drawing lands where it should.
void gfx_viewport(int x, int y, int w, int h);
void gfx_viewport_screen(void);

void gfx_begin_3d(void);
void gfx_begin_2d(void);

// Closes the display list and waits for the GPU to finish drawing.
void gfx_finish(void);

// Copies the finished frame (RGB565, MC_BUF_STRIDE pixels per line) into `dst`,
// which must be 64-byte aligned RAM of MC_BUF_STRIDE * MC_SCREEN_H shorts.
// Call between gfx_finish() and gfx_present().
void gfx_copy_frame(unsigned short *dst);
// Any frame size: dst holds stride * frame height shorts (see r_size).
void gfx_copy_frame_stride(unsigned short *dst, int stride);

// Native mode (before gfx_init): the frame is the window's own pixels and follows
// resizes; RGBA8 without dither. Off = the fixed retro frame scaled to the window.
void gfx_set_native(int on);
int gfx_native(void);

// Waits for the next vblank slot and swaps. frame_ms (CPU + GPU time this frame)
// drives the adaptive 60 -> 30 FPS fallback.
void gfx_present(float frame_ms);

// Settings: hold a steady 30 regardless of headroom.
void gfx_set_locked_30(int on);

// 1 while running at 60 FPS, 2 while held at 30.
int gfx_vblanks_per_frame(void);

#endif