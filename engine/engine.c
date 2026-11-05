#include "engine/engine.h"
#include "engine/platform/platform.h"
#include "engine/platform/input.h"
#include "engine/render/gfx.h"
#include "engine/render/render_api.h"
#include "engine/console/console.h"

#include <string.h>

static int s_running = 1;
static int s_inited;

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
    // The configured size is the frame (retro) or the starting/offscreen size (native).
    if (cfg->width > 0 && cfg->height > 0) r_set_frame_size(cfg->width, cfg->height);
    if (cfg->native) gfx_set_native(1);
    if (gfx_init() != 0)
        return -2;
    input_init();
    console_init(0);
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

int engine_loop(EngineUpdateFn update, EngineDrawFn draw, void *user, int frame_limit)
{
    Input in;
    double last = plat_time();
    int frames = 0;
    while (engine_running()) {
        double now = plat_time();
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        console_poll();
        input_poll(&in);
        if (update)
            update(dt, user);
        gfx_begin_3d();
        if (draw)
            draw(user);
        gfx_begin_2d();
        gfx_finish();
        gfx_present(dt * 1000.0f);
        frames++;
        if (frame_limit > 0 && frames >= frame_limit)
            break;
    }
    return 0;
}
