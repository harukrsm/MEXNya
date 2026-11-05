#include "debug_overlay.h"
#include "stats.h"
#include "text.h"
#include "engine/platform/system.h"
#include "engine/render/render_config.h"

#include "engine/platform/platform.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#define MAX_LINES 8

static char s_lines[MAX_LINES][64];
static int s_line_count;

static double s_window_start;
static int s_window_frames;
static float s_fps;
static float s_cpu_ms, s_cpu_peak, s_cpu_peak_shown;

void overlay_frame(float cpu_ms)
{
    double now = sys_time();
    if (s_window_start == 0.0)
        s_window_start = now;
    s_window_frames++;
    s_cpu_ms = cpu_ms;
    if (cpu_ms > s_cpu_peak)
        s_cpu_peak = cpu_ms;
    if (now - s_window_start >= 1.0) {
        s_fps = (float)(s_window_frames / (now - s_window_start));
        s_window_frames = 0;
        s_window_start = now;
        s_cpu_peak_shown = s_cpu_peak;
        s_cpu_peak = 0.0f;
    }
}

float overlay_last_ms(void) { return s_cpu_ms; }
float overlay_fps(void) { return s_fps; }

void overlay_line(const char *fmt, ...)
{
    if (s_line_count >= MAX_LINES)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_lines[s_line_count++], sizeof(s_lines[0]), fmt, ap);
    va_end(ap);
}

static int s_visible;

void overlay_set_visible(int visible) { s_visible = visible; }
int overlay_visible(void) { return s_visible; }

void overlay_draw(void)
{
    if (s_visible) {
        int heap_k = (int)(plat_used_mem() / 1024);
        int y = 4;
        const unsigned int c = 0xD7E2E5;
        text_printf(4, y, c, "FPS %4.1f  CPU %4.1fMS (PEAK %4.1f)", s_fps, s_cpu_ms, s_cpu_peak_shown);
        y += TEXT_LINE_H;
        text_printf(4, y, c, "TRI %5d  DRAW %3d  TEX %3d", g_rstats.triangles, g_rstats.draw_calls,
                    g_rstats.texture_binds);
        y += TEXT_LINE_H;
        text_printf(4, y, c, "HEAP %5dK USED", heap_k);
        y += TEXT_LINE_H;
        for (int i = 0; i < s_line_count; i++, y += TEXT_LINE_H)
            text_print(4, y, 0xC9A63C, s_lines[i]);
    }
    s_line_count = 0;
}

void overlay_perf_draw(void)
{
#if MC_PERF_HUD
    if (s_visible)
        return;  // the full panel already shows all of this
    // Line 1: frame rate, CPU time, triangles, draw calls. Line 2: heap in use (KiB).
    text_printf(2, 2, MC_PERF_HUD_RGB, "FPS %4.1f %4.1fMS TRI %5d DRAW %3d", s_fps, s_cpu_ms,
                g_rstats.triangles, g_rstats.draw_calls);
    text_printf(2, 2 + TEXT_LINE_H, MC_PERF_HUD_RGB, "MEM %dK USED", (int)(plat_used_mem() / 1024));
#endif
}