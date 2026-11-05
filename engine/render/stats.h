// Per-frame render counters for the debug overlay.
#ifndef MC_RENDER_STATS_H
#define MC_RENDER_STATS_H

typedef struct {
    int triangles;
    int draw_calls;
    int texture_binds;
    int vertices;      // vertices handed to the renderer (each one is transformed)
    int world_draws;   // of draw_calls, the ones for chunk geometry
} RenderStats;

extern RenderStats g_rstats;

static inline void rstats_reset(void)
{
    g_rstats.triangles = 0;
    g_rstats.draw_calls = 0;
    g_rstats.texture_binds = 0;
    g_rstats.vertices = 0;
    g_rstats.world_draws = 0;
}

#endif
