// MEXNya engine: start-up, the main loop and shutdown. No SDL, OpenGL or Lua types
// appear in any engine header.
#ifndef MC_ENGINE_ENGINE_H
#define MC_ENGINE_ENGINE_H

#include <stdint.h>

#define MX_ENGINE_VERSION "0.1.0"

// width x height: the frame in retro mode (scaled up to the window), or the first and
// headless size in native mode. scale: 0 = largest whole multiple that fits.
// fixed_hz: 0 = variable step from frame time; a game uses 60.
typedef struct {
    int width, height, scale;
    int headless;
    const char *title;
    const char *org;
    const char *app;
    const char *backend;   // "software", "gl" or "auto"
    int native;            // 1 = native resolution (desktop tools, hi-res games)
    int fixed_hz;
    int max_catchup;       // ticks per frame before the backlog is dropped (default 4)
    int pause_on_focus_lost;
} EngineConfig;

typedef void (*EngineUpdateFn)(float dt, void *user);
typedef void (*EngineDrawFn)(void *user);
typedef void (*EngineTickFn)(float dt, void *user);
typedef void (*EngineRenderFn)(float alpha, void *user);

typedef struct {
    float input_ms;
    float tick_ms;
    float render_ms;
    float present_ms;
    int ticks;
} EngineFrameStats;

int engine_init(const EngineConfig *cfg);
void engine_shutdown(void);
// Runs until the window closes, engine_request_quit(), or frame_limit > 0 is reached.
int engine_loop(EngineUpdateFn update, EngineDrawFn draw, void *user, int frame_limit);
// Same loop with an interpolated render step and a fixed timestep when fixed_hz > 0.
int engine_loop_ex(EngineTickFn tick, EngineRenderFn render, void *user, int frame_limit);
// Advances one frame by dt seconds (tests and embedders that own their clock).
int engine_step(double dt, EngineTickFn tick, EngineRenderFn render, void *user);
void engine_reset_time(void);
void engine_request_quit(void);
int engine_running(void);
double engine_time(void);
void engine_set_time_scale(float scale);
float engine_time_scale(void);
int engine_tick_count(void);
uint64_t engine_tick_hash(void);
float engine_frame_alpha(void);
const EngineFrameStats *engine_frame_stats(void);

#endif
