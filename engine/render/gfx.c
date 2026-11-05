// Frame setup shared by every renderer: projection, fog, the 3D/2D passes and
// frame pacing.
#include "gfx.h"
#include "render_api.h"
#include "engine/render/render_config.h"
#include "engine/platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t s_fog_color;
static float s_fog_near = 20.0f, s_fog_far = 80.0f;
static int s_vblanks = MC_FRAME_VBLANKS_FAST;
static int s_locked_30;
static int s_headless;
static int s_missed;
static int s_cheap_frames;
static double s_next_frame;
static char s_backend[16] = "auto";
static int s_native;

// Native mode: the frame follows the window's drawable size (desktop tools, high
// resolution games). Retro mode keeps the fixed internal size and scales it up.
void gfx_set_native(int on)
{
    s_native = on ? 1 : 0;
    r_set_native(s_native);
    plat_video_set_native(s_native);
}

int gfx_native(void) { return s_native; }

static void sync_native_size(void)
{
    if (!s_native || s_headless) return;
    int dw = 0, dh = 0, fw = 0, fh = 0;
    if (plat_video_drawable(&dw, &dh) != 0 || dw < 16 || dh < 16) return;
    r_size(&fw, &fh);
    if (dw == fw && dh == fh) return;
    if (r_set_frame_size(dw, dh) == 0) plat_video_set_internal(dw, dh);
}

void gfx_set_headless(int on) { s_headless = on; }
void gfx_set_backend(const char *name)
{
    snprintf(s_backend, sizeof(s_backend), "%s", name ? name : "auto");
}

static int open_video_for_backend(void)
{
    if (s_headless)
        return 0;
    if (!strcmp(r_backend_name(), "opengl"))
        return 0;  // r_init already created the GL window/context
    int w = MC_LOWRES_W, h = MC_LOWRES_H;
    r_size(&w, &h);
    return plat_video_open(w, h, 0, 0);
}

int gfx_init(void)
{
    if (s_headless && strcmp(s_backend, "software") != 0)
        snprintf(s_backend, sizeof(s_backend), "software");
    if (r_init(s_backend) != 0) {
        if (r_init("software") != 0)
            return -1;
    }
    if (open_video_for_backend() != 0)
        return -2;
    sync_native_size();
    s_fog_color = MC_RGB(0x34383C);  // game calls gfx_set_fog() every frame
    s_next_frame = plat_time();
    r_matrix_mode(R_MATRIX_PROJECTION);
    r_load_identity();
    return 0;
}

int gfx_switch_backend(const char *name)
{
    r_shutdown();
    plat_video_close();
    snprintf(s_backend, sizeof(s_backend), "%s", name ? name : "auto");
    if (r_init(s_backend) != 0) {
        if (r_init("software") != 0)
            return -1;
    }
    if (open_video_for_backend() != 0)
        return -2;
    r_matrix_mode(R_MATRIX_PROJECTION);
    r_load_identity();
    return 0;
}

void gfx_shutdown(void)
{
    r_shutdown();
    if (!s_headless)
        plat_video_close();
}

const char *gfx_backend_name(void) { return r_backend_name(); }

void gfx_set_fog(float near_m, float far_m, unsigned int rgb_hex)
{
    s_fog_near = near_m;
    s_fog_far = far_m;
    s_fog_color = MC_RGB(rgb_hex);
}

float gfx_fog_far(void) { return s_fog_far; }

void gfx_set_projection(float fov_deg)
{
    r_matrix_mode(R_MATRIX_PROJECTION);
    r_load_identity();
    int w = MC_LOWRES_W, h = MC_LOWRES_H;
    r_size(&w, &h);
    r_perspective(fov_deg, (float)w / (float)h, MC_CAM_NEAR,
                  s_fog_far + MC_CAM_FAR_MARGIN);
    r_set_near(MC_CAM_NEAR);
}

void gfx_viewport(int x, int y, int w, int h)
{
    r_viewport(x, y, w, h);
    r_scissor(x, y, w, h);
}

void gfx_viewport_screen(void)
{
    int w, h;
    r_ui_size(&w, &h);
    gfx_viewport(0, 0, w, h);
}

void gfx_begin_3d(void)
{
    sync_native_size();
    r_begin_3d();
    r_clear(s_fog_color, 0);
    r_set_depth_test(1);
    r_set_depth_write(1);
    r_set_cull(1);
    r_set_alpha_test(0);
    r_set_blend(R_BLEND_NONE);
    r_set_fog(1);
    r_set_fog_params(s_fog_near, s_fog_far, s_fog_color);
    r_set_uv_scale(1.0f, 1.0f);
    r_set_uv_offset(0.0f, 0.0f);
}

void gfx_begin_2d(void)
{
    r_begin_2d();
    r_set_fog(0);
    r_set_depth_test(0);
    r_set_cull(0);
    r_set_blend(R_BLEND_NONE);
    r_set_alpha_test(0);
}

void gfx_finish(void) { r_end_frame(); }

void gfx_copy_frame(unsigned short *dst)
{
    r_copy_frame(dst, MC_BUF_STRIDE);
}

void gfx_copy_frame_stride(unsigned short *dst, int stride) { r_copy_frame(dst, stride); }

static void pace(void)
{
    // Steady 30 when frames are too expensive, back
    // to 60 when there is headroom. Sleep, don't spin.
    int target = s_vblanks == MC_FRAME_VBLANKS_FAST ? 60 : 30;
    s_next_frame += 1.0 / target;
    double now = plat_time();
    if (s_next_frame < now - 0.25)
        s_next_frame = now;  // fell far behind (window drag, breakpoint): don't catch up
    double wait = s_next_frame - now;
    if (wait > 0.0)
        plat_sleep_ms((int)(wait * 1000.0));
    else
        s_next_frame = now;
}

void gfx_set_locked_30(int on)
{
    s_locked_30 = on;
    if (on)
        s_vblanks = MC_FRAME_VBLANKS_SAFE;
}

int gfx_vblanks_per_frame(void) { return s_vblanks; }

void gfx_present(float frame_ms)
{
    if (!s_locked_30) {
        if (s_vblanks == MC_FRAME_VBLANKS_FAST) {
            s_missed = frame_ms > 16.7f ? s_missed + 1 : 0;
            if (s_missed >= MC_FRAME_MISS_TO_DROP) {
                s_vblanks = MC_FRAME_VBLANKS_SAFE;
                s_cheap_frames = 0;
            }
        } else {
            s_cheap_frames = frame_ms < MC_FRAME_CPU_MS_RECOVER ? s_cheap_frames + 1 : 0;
            if (s_cheap_frames >= 30) {
                s_vblanks = MC_FRAME_VBLANKS_FAST;
                s_missed = 0;
            }
        }
    } else {
        s_vblanks = MC_FRAME_VBLANKS_SAFE;
    }
#ifdef MC_DESKTOP
    if (!s_headless)
        pace();
#endif
    r_present();
}