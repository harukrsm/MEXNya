// MEXNya engine: start-up, the main loop and shutdown. No SDL, OpenGL or Lua types
// appear in any engine header.
#ifndef MC_ENGINE_ENGINE_H
#define MC_ENGINE_ENGINE_H

#define MX_ENGINE_VERSION "0.1.0"

// width x height: the frame in retro mode (scaled up to the window), or the first and
// headless size in native mode. scale: 0 = largest whole multiple that fits.
typedef struct {
    int width, height, scale;
    int headless;
    const char *title;
    const char *org;
    const char *app;
    const char *backend;   // "software", "gl" or "auto"
    int native;            // 1 = native resolution (desktop tools, hi-res games)
} EngineConfig;

typedef void (*EngineUpdateFn)(float dt, void *user);
typedef void (*EngineDrawFn)(void *user);

int engine_init(const EngineConfig *cfg);
void engine_shutdown(void);
// Runs until the window closes, engine_request_quit(), or frame_limit > 0 is reached.
int engine_loop(EngineUpdateFn update, EngineDrawFn draw, void *user, int frame_limit);
void engine_request_quit(void);
int engine_running(void);
double engine_time(void);

#endif
