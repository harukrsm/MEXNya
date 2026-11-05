// Internal render resolution, generic frame pacing and camera defaults.
// Game-specific values stay in src/config.h, which includes this file.
#ifndef MC_RENDER_CONFIG_H
#define MC_RENDER_CONFIG_H

// A target may override the size (the editor builds the engine at 960x544).
#ifndef MC_SCREEN_W
#define MC_SCREEN_W        480
#endif
#ifndef MC_SCREEN_H
#define MC_SCREEN_H        272
#endif
#ifndef MC_BUF_STRIDE
#define MC_BUF_STRIDE      512
#endif

#ifndef MC_RENDER_SCALE
#define MC_RENDER_SCALE    1
#endif
#define MC_LOWRES_W        (MC_SCREEN_W / MC_RENDER_SCALE)
#define MC_LOWRES_H        (MC_SCREEN_H / MC_RENDER_SCALE)
#define MC_LOWRES_STRIDE   (MC_BUF_STRIDE / MC_RENDER_SCALE)

#define MC_FRAME_VBLANKS_FAST    1
#define MC_FRAME_VBLANKS_SAFE    2
#define MC_FRAME_MISS_TO_DROP    3
#define MC_FRAME_CPU_MS_RECOVER  11.0f
#define MC_LOGIC_HZ              60
#define MC_MAX_LOGIC_STEPS       4
#define MC_MAX_FRAME_DT          0.1f

#define MC_CAM_NEAR              0.5f
#define MC_CAM_FAR_MARGIN        10.0f

#define MC_RGB(hex) (0xFF000000u | (((hex) & 0xFFu) << 16) | ((hex) & 0xFF00u) | (((hex) >> 16) & 0xFFu))

#ifndef MC_PERF_HUD
#define MC_PERF_HUD 0
#endif
#define MC_PERF_HUD_RGB   0xD7E2E5

#endif
