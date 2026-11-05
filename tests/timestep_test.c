#include "engine/engine.h"

#include <math.h>
#include <stdio.h>

static int s_failures;
static float s_alpha;
static int s_renders;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failures++; \
        } \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabsf((float)(a) - (float)(b)) <= (eps))

typedef struct {
    double total;
    int updates;
    float last;
} Sim;

static void tick(float dt, void *user)
{
    Sim *s = (Sim *)user;
    s->total += dt;
    s->updates++;
    s->last = dt;
}

static void render(float alpha, void *user)
{
    (void)user;
    s_alpha = alpha;
    s_renders++;
}

static void run_steps(int count, double dt, Sim *sim)
{
    for (int i = 0; i < count; i++) engine_step(dt, tick, render, sim);
}

int main(void)
{
    EngineConfig cfg = {320, 180, 0, 1, "timestep", "MEXNya", "timestep", "software", 0, 60, 4, 0};
    if (engine_init(&cfg) != 0) {
        printf("timestep_test: engine_init failed\n");
        return 1;
    }

    Sim sim = {0};
    engine_reset_time();
    run_steps(600, 1.0 / 60.0, &sim);
    CHECK(engine_tick_count() == 600);
    CHECK(sim.updates == 600);
    CHECK_NEAR(sim.total, 10.0, 0.01);
    CHECK_NEAR(sim.last, 1.0f / 60.0f, 1e-6f);
    CHECK(engine_frame_stats()->ticks == 1);

    sim = (Sim){0};
    engine_reset_time();
    run_steps(300, 1.0 / 30.0, &sim);
    CHECK(engine_tick_count() == 600);
    CHECK(sim.updates == 600);
    CHECK(engine_frame_stats()->ticks == 2);

    sim = (Sim){0};
    engine_reset_time();
    run_steps(1440, 1.0 / 144.0, &sim);
    int ticks144 = engine_tick_count();
    CHECK(ticks144 >= 599 && ticks144 <= 601);

    sim = (Sim){0};
    engine_reset_time();
    s_renders = 0;
    run_steps(600, 1.0 / 60.0, &sim);
    uint64_t hash_a = engine_tick_hash();
    int updates_a = sim.updates;
    double total_a = sim.total;
    sim = (Sim){0};
    engine_reset_time();
    run_steps(600, 1.0 / 60.0, &sim);
    CHECK(engine_tick_hash() == hash_a);
    CHECK(sim.updates == updates_a);
    CHECK(sim.total == total_a);
    CHECK(s_renders == 1200);

    engine_reset_time();
    engine_set_time_scale(0.0f);
    sim = (Sim){0};
    run_steps(120, 1.0 / 60.0, &sim);
    CHECK(engine_tick_count() == 0);
    CHECK(sim.updates == 0);
    engine_set_time_scale(0.5f);
    run_steps(120, 1.0 / 60.0, &sim);
    CHECK(engine_tick_count() == 60);
    engine_set_time_scale(1.0f);

    engine_reset_time();
    sim = (Sim){0};
    engine_step(1.0, tick, render, &sim);
    CHECK(engine_tick_count() == 4);
    CHECK(engine_frame_stats()->ticks == 4);

    engine_reset_time();
    sim = (Sim){0};
    engine_step(1.0 / 120.0, tick, render, &sim);
    CHECK(engine_tick_count() == 0);
    CHECK_NEAR(engine_frame_alpha(), 0.5f, 0.01f);
    engine_step(1.0 / 120.0, tick, render, &sim);
    CHECK(engine_tick_count() == 1);
    CHECK(engine_frame_alpha() < 0.01f);

    engine_shutdown();
    if (s_failures) {
        printf("timestep_test: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("timestep_test: all checks passed\n");
    return 0;
}
