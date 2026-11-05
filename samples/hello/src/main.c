// hello: a MEXNya game. Start here.
//
// The scenes in mexnya/ are made with the MEXNya Editor (`mexnya run`); this file loads
// them with the scene runtime and adds the game: move the "player" object with WASD or
// the left stick, jump with Space, press E to fire the "main.use" port (a Line shows the
// "hud" scene), Escape to quit.
#include "engine/engine.h"
#include "engine/platform/platform.h"
#include "engine/platform/input.h"
#include "engine/platform/file.h"
#include "engine/platform/screenshot.h"
#include "engine/core/png.h"
#include "engine/render/gfx.h"
#include "engine/render/font.h"
#include "engine/render/render_api.h"
#include "engine/scene/scene.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    MxProject *project;
    int main_scene;
    MxCamera cam;
    float vy;            // player's vertical speed
    float heading;       // degrees
    int uses;
} Game;

static void on_port(MxProject *p, const char *node, const char *port, void *user)
{
    (void)p;
    Game *g = (Game *)user;
    printf("port reached: %s.%s\n", node, port);
    if (!strcmp(port, "show")) g->uses++;
}

static void update(float dt, void *user)
{
    Game *g = (Game *)user;
    Input in;
    input_get_last(&in);
    if (in.pressed & ACT_BIT(ACT_BACK)) engine_request_quit();
    Json *player = mx_object(g->project, g->main_scene, "player");
    if (!player) return;
    float pos[3] = {0, 0.5f, 0};
    json_getv(player, "pos", pos, 3);
    // Move relative to the camera: forward is away from the viewer.
    float yaw = g->cam.yaw * 0.017453293f;
    float speed = (in.held & ACT_BIT(ACT_SPRINT)) ? 8.0f : 4.0f;
    float fx = -sinf(yaw), fz = -cosf(yaw), rx = cosf(yaw), rz = -sinf(yaw);
    float mx = in.move_x * speed * dt, mz = -in.move_y * speed * dt;
    pos[0] += fx * mz + rx * mx;
    pos[2] += fz * mz + rz * mx;
    if (mx != 0 || mz != 0) g->heading = atan2f(fx * mz + rx * mx, fz * mz + rz * mx) * 57.29578f;
    // Jump and fall back to the ground (y = 0.5 for a unit cube).
    if ((in.pressed & ACT_BIT(ACT_JUMP)) && pos[1] <= 0.5f) g->vy = 6.0f;
    g->vy -= 18.0f * dt;
    pos[1] += g->vy * dt;
    if (pos[1] < 0.5f) { pos[1] = 0.5f; g->vy = 0; }
    json_setv(player, "pos", pos, 3);
    float rot[3] = {0, g->heading, 0};
    json_setv(player, "rot", rot, 3);
    if (in.pressed & ACT_BIT(ACT_USE)) mx_emit(g->project, "main.use");
    // The camera follows the player.
    for (int i = 0; i < 3; i++) g->cam.target[i] += (pos[i] - g->cam.target[i]) * (1.0f - expf(-6.0f * dt));
}

static void draw(void *user)
{
    Game *g = (Game *)user;
    int w = 0, h = 0;
    r_ui_size(&w, &h);
    mx_project_draw(g->project, &g->cam, w, h);  // 3D layers, then the 2D layers
    font_drawf(12, h - 28, 0xC8CBD0, "WASD move  Space jump  E use (%d)  Esc quit", g->uses);
}

int main(int argc, char **argv)
{
    int headless = 0, frames = 0;
    const char *shot = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) { shot = argv[++i]; headless = 1; }
    }
    // native = 1: the window's own resolution. For a fixed low-resolution look use
    // native = 0 and a small width x height (for example 480 x 272).
    EngineConfig cfg = {1280, 720, 0, headless, "hello", "MEXNya", "hello",
                        headless ? "software" : "auto", 1};
    if (engine_init(&cfg) != 0) {
        fprintf(stderr, "engine_init failed\n");
        return 1;
    }
    char dir[1024], path[1200];
    plat_exe_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/engine-resources/fonts/mx12.mxf", dir);
    font_load(path);
    if (plat_video_density() >= 1.75f) r_set_ui_scale(2);
    file_set_game_dir(argv[0]);                  // data: <exe>/assets
    snprintf(path, sizeof(path), "%s/mexnya", dir);
    Game g;
    memset(&g, 0, sizeof(g));
    g.project = mx_project_load(path);
    if (!g.project) {
        fprintf(stderr, "cannot load the project in %s\n", path);
        engine_shutdown();
        return 1;
    }
    g.main_scene = mx_scene_find(g.project, "main");
    mx_scene_camera(g.project, g.main_scene, &g.cam);
    mx_on_port(g.project, on_port, &g);
    gfx_set_fog(500, 1000, 0x3A3D42);            // the clear colour
    engine_loop(update, draw, &g, frames);
    if (shot) {
        int w = 0, h = 0;
        r_size(&w, &h);
        unsigned short *fb = (unsigned short *)malloc(sizeof(unsigned short) * (size_t)w * (size_t)h);
        if (fb) {
            gfx_copy_frame_stride(fb, w);
            png_save_565(shot, fb, w, h, w);
            free(fb);
        }
    }
    mx_project_free(g.project);
    engine_shutdown();
    return 0;
}
