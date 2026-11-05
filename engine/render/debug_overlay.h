// SELECT-toggled debug panel: FPS, CPU time, triangles, draw calls, memory.
#ifndef MC_RENDER_DEBUG_OVERLAY_H
#define MC_RENDER_DEBUG_OVERLAY_H

// Call once per frame with the CPU time spent before waiting for vblank.
void overlay_frame(float cpu_ms);

// Extra lines from game systems (e.g. "FOG: STANDARD"), cleared each frame.
void overlay_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Queues the panel text (no-op when hidden). Call in the 2D pass before text_flush().
void overlay_draw(void);

void overlay_set_visible(int visible);

// The always-on MC_PERF_HUD line (no-op when MC_PERF_HUD is 0 or the full panel is showing).
void overlay_perf_draw(void);

// The last frame's CPU + GPU time in ms (benchmarks).
float overlay_last_ms(void);
float overlay_fps(void);
int overlay_visible(void);

#endif
