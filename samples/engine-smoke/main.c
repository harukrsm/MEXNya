// Engine smoke sample: no game code, no game assets. Opens a window, reads input,
// draws a triangle through render_api, plays a test tone and runs a console command.
#include "engine/engine.h"
#include "engine/platform/platform.h"
#include "engine/platform/input.h"
#include "engine/platform/screenshot.h"
#include "engine/render/render_api.h"
#include "engine/render/gfx.h"
#include "engine/console/console.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float s_phase;
static int s_frames;

static void fill_tone(int16_t *out, int frames, void *user)
{
    (void)user;
    for (int i = 0; i < frames; i++) {
        float v = sinf(s_phase) * 0.08f;
        s_phase += 2.0f * 3.14159265f * 440.0f / 44100.0f;
        if (s_phase > 2.0f * 3.14159265f) s_phase -= 2.0f * 3.14159265f;
        int16_t s = (int16_t)(v * 32767.0f);
        out[i * 2] = s;
        out[i * 2 + 1] = s;
    }
}

static void update(float dt, void *user)
{
    (void)user; (void)dt;
    Input in;
    input_poll(&in);
    if (in.pressed)
        console_print("input pressed 0x%x", in.pressed);
}

static void draw(void *user)
{
    (void)user;
    gfx_set_projection(60.0f);
    r_matrix_mode(R_MATRIX_VIEW);
    r_load_identity();
    RVec3 eye = {0, 0, 4}, at = {0, 0, 0}, up = {0, 1, 0};
    r_look_at(&eye, &at, &up);
    r_matrix_mode(R_MATRIX_MODEL);
    r_load_identity();
    r_rotate_y(0.6f);
    RVertex tri[3] = {
        {0.0f, 1.2f, 0.0f, 0.0f, 0.0f, 0xFFE8C8A0},
        {-1.1f, -0.9f, 0.0f, 0.0f, 0.0f, 0xFF80B0D0},
        {1.1f, -0.9f, 0.0f, 0.0f, 0.0f, 0xFF60C080},
    };
    r_bind_texture(NULL);
    r_set_cull(0);
    r_draw(R_PRIM_TRIANGLES, tri, 3);
    r_set_cull(1);
}

static int cmd_hello(int argc, char **argv)
{
    (void)argc; (void)argv;
    console_print("engine-smoke console says hello");
    return 0;
}

int main(int argc, char **argv)
{
    int headless = 0, frames = 60;
    const char *shot = NULL, *backend = "software";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) { shot = argv[++i]; headless = 1; }
        else if (!strcmp(argv[i], "--backend") && i + 1 < argc) backend = argv[++i];
    }
    EngineConfig cfg = {480, 272, 2, headless, "MEXNya Engine Smoke", "MEXNya", "EngineSmoke", backend};
    if (engine_init(&cfg) != 0) {
        fprintf(stderr, "engine-smoke: init failed\n");
        return 1;
    }
    console_register("hello", "", "print a greeting", cmd_hello);
    console_exec_line("hello");
    int audio = plat_audio_open(44100, 1024, fill_tone, NULL);
    console_print("audio device: %s", audio == 0 ? "open" : "missing (continuing silently)");
    engine_loop(update, draw, NULL, frames);
    if (shot) {
        unsigned short *fb = malloc(512 * 272 * 2);
        if (fb) {
            gfx_copy_frame(fb);
            screenshot_save_565(shot, fb, 480, 272, 512);
            free(fb);
        }
    }
    plat_audio_close();
    engine_shutdown();
    printf("engine-smoke: %d frames, backend %s\n", frames, backend);
    return 0;
}
