#include "engine/engine.h"
#include "engine/core/log.h"
#include "engine/platform/platform.h"
#include "engine/platform/input.h"
#include "engine/render/gfx.h"
#include "engine/render/render_api.h"
#include "engine/console/console.h"

#include <string.h>

static int s_running = 1;
static int s_inited;
static int s_fixed_hz;
static int s_max_catchup = 4;
static int s_pause_on_focus;
static int s_catchup_warned;
static float s_time_scale = 1.0f;
static float s_alpha = 1.0f;
static double s_accumulator;
static int s_ticks;
static uint64_t s_tick_hash = 1469598103934665603ull;
static EngineFrameStats s_stats;

static uint64_t hash_bytes(uint64_t h, const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static void tick_advance(float dt)
{
    uint32_t n = (uint32_t)(s_ticks + 1);
    s_ticks = (int)n;
    s_tick_hash = hash_bytes(s_tick_hash, &dt, sizeof(dt));
    s_tick_hash = hash_bytes(s_tick_hash, &n, sizeof(n));
}

int engine_init(const EngineConfig *cfg)
{
    if (!cfg)
        return -1;
    plat_set_identity(cfg->org ? cfg->org : "MEXNya", cfg->app ? cfg->app : "Engine");
    plat_video_set_title(cfg->title ? cfg->title : cfg->app);
    if (plat_init(0, NULL) != 0)
        return -1;
    gfx_set_headless(cfg->headless);
    gfx_set_backend(cfg->backend ? cfg->backend : "software");
    if (cfg->width > 0 && cfg->height > 0) r_set_frame_size(cfg->width, cfg->height);
    if (cfg->native) gfx_set_native(1);
    if (gfx_init() != 0)
        return -2;
    input_init();
    console_init(0);
    s_fixed_hz = cfg->fixed_hz > 0 ? cfg->fixed_hz : 0;
    s_max_catchup = cfg->max_catchup > 0 ? cfg->max_catchup : 4;
    s_pause_on_focus = cfg->pause_on_focus_lost;
    engine_reset_time();
    s_running = 1;
    s_inited = 1;
    return 0;
}

void engine_shutdown(void)
{
    if (!s_inited)
        return;
    console_shutdown();
    gfx_shutdown();
    plat_shutdown();
    s_inited = 0;
}

int engine_running(void) { return s_running && plat_running(); }
void engine_request_quit(void) { s_running = 0; }
double engine_time(void) { return plat_time(); }

void engine_reset_time(void)
{
    s_accumulator = 0.0;
    s_ticks = 0;
    s_tick_hash = 1469598103934665603ull;
    s_alpha = 1.0f;
    s_catchup_warned = 0;
    memset(&s_stats, 0, sizeof(s_stats));
}

void engine_set_time_scale(float scale)
{
    if (scale < 0.0f) scale = 0.0f;
    if (scale > 8.0f) scale = 8.0f;
    s_time_scale = scale;
}

float engine_time_scale(void) { return s_time_scale; }
int engine_tick_count(void) { return s_ticks; }
uint64_t engine_tick_hash(void) { return s_tick_hash; }
float engine_frame_alpha(void) { return s_alpha; }
const EngineFrameStats *engine_frame_stats(void) { return &s_stats; }

static float ms_since(double start)
{
    return (float)((plat_time() - start) * 1000.0);
}

static void run_frame(double dt, EngineTickFn tick, EngineRenderFn render, void *user)
{
    Input in;
    double t0;
    float fixed = s_fixed_hz > 0 ? 1.0f / (float)s_fixed_hz : 0.0f;
    int paused;

    if (dt < 0.0) dt = 0.0;
    if (dt > 0.25) dt = 0.25;
    memset(&s_stats, 0, sizeof(s_stats));
    console_poll();
    t0 = plat_time();
    input_poll(&in);
    s_stats.input_ms = ms_since(t0);
    paused = s_time_scale <= 0.0f || (s_pause_on_focus && !plat_window_focused());

    t0 = plat_time();
    if (fixed > 0.0f) {
        if (!paused) {
            int steps = 0;
            double epsilon = (double)fixed * 1e-6;
            s_accumulator += dt * s_time_scale;
            if (s_accumulator > 0.25) s_accumulator = 0.25;
            while (s_accumulator + epsilon >= fixed && steps < s_max_catchup) {
                if (steps > 0) input_clear_edges();
                if (tick) tick(fixed, user);
                tick_advance(fixed);
                s_accumulator -= fixed;
                steps++;
            }
            if (s_accumulator + epsilon >= fixed) {
                s_accumulator = 0.0;
                if (!s_catchup_warned) {
                    MX_LOGW("engine", "fixed step fell behind at %d Hz; dropping the backlog", s_fixed_hz);
                    s_catchup_warned = 1;
                }
            }
            if (s_accumulator < 0.0) s_accumulator = 0.0;
            s_alpha = (float)(s_accumulator / fixed);
            if (s_alpha < 0.0f) s_alpha = 0.0f;
            if (s_alpha > 1.0f) s_alpha = 1.0f;
            s_stats.ticks = steps;
        } else {
            s_alpha = 1.0f;
        }
    } else if (!paused) {
        float step = (float)(dt > 0.1 ? 0.1 : dt) * s_time_scale;
        if (tick) tick(step, user);
        tick_advance(step);
        s_alpha = 1.0f;
        s_stats.ticks = 1;
    }
    s_stats.tick_ms = ms_since(t0);

    t0 = plat_time();
    gfx_begin_3d();
    if (render) render(s_alpha, user);
    gfx_begin_2d();
    gfx_finish();
    s_stats.render_ms = ms_since(t0);

    t0 = plat_time();
    gfx_present((float)(dt * 1000.0));
    s_stats.present_ms = ms_since(t0);
}

int engine_step(double dt, EngineTickFn tick, EngineRenderFn render, void *user)
{
    run_frame(dt, tick, render, user);
    return 0;
}

int engine_loop_ex(EngineTickFn tick, EngineRenderFn render, void *user, int frame_limit)
{
    double last = plat_time();
    int frames = 0;
    engine_reset_time();
    while (engine_running()) {
        double now = plat_time();
        double dt = now - last;
        last = now;
        run_frame(dt, tick, render, user);
        frames++;
        if (frame_limit > 0 && frames >= frame_limit)
            break;
    }
    return 0;
}

typedef struct {
    EngineUpdateFn update;
    EngineDrawFn draw;
    void *user;
} LegacyLoop;

static void legacy_tick(float dt, void *user)
{
    LegacyLoop *l = (LegacyLoop *)user;
    if (l->update) l->update(dt, l->user);
}

static void legacy_render(float alpha, void *user)
{
    LegacyLoop *l = (LegacyLoop *)user;
    (void)alpha;
    if (l->draw) l->draw(l->user);
}

int engine_loop(EngineUpdateFn update, EngineDrawFn draw, void *user, int frame_limit)
{
    LegacyLoop l = {update, draw, user};
    return engine_loop_ex(legacy_tick, legacy_render, &l, frame_limit);
}
