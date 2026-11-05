#include "engine/world/world.h"
#include "engine/world/components.h"
#include "engine/world/instantiate.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failures++; \
        } \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabsf((float)(a) - (float)(b)) <= (eps))

static void test_entities(void)
{
    MxWorld *w = mx_world_create();
    CHECK(w != NULL);
    if (!w) return;
    enum { N = 100000 };
    MxEntity *handles = (MxEntity *)mx_mem_alloc(sizeof(MxEntity) * N);
    CHECK(handles != NULL);
    if (!handles) {
        mx_world_destroy(w);
        return;
    }
    for (int i = 0; i < N; i++) {
        handles[i] = mx_entity_create(w);
        if (!mx_entity_alive(w, handles[i])) CHECK(0);
    }
    CHECK(mx_entity_count(w) == N);
    CHECK(mx_entity_capacity(w) >= N);
    MxEntity dead = handles[8];
    for (int i = 0; i < N; i += 2) mx_entity_destroy(w, handles[i]);
    CHECK(mx_entity_count(w) == N / 2);
    CHECK(!mx_entity_alive(w, dead));
    CHECK(mx_comp_get(w, dead, MX_COMP_TRANSFORM) == NULL);
    CHECK(mx_entity_find(w, "nothing").index == MX_ENTITY_NONE.index);
    MxEntity fresh = mx_entity_create(w);
    CHECK(mx_entity_alive(w, fresh));
    CHECK(fresh.gen != dead.gen || fresh.index != dead.index);
    mx_mem_free(handles);
    mx_world_destroy(w);
}

static void test_names(void)
{
    MxWorld *w = mx_world_create();
    enum { N = 100000 };
    MxEntity *handles = (MxEntity *)mx_mem_alloc(sizeof(MxEntity) * N);
    CHECK(w != NULL && handles != NULL);
    if (!w || !handles) {
        mx_mem_free(handles);
        mx_world_destroy(w);
        return;
    }
    for (int i = 0; i < N; i++) {
        char name[32];
        snprintf(name, sizeof(name), "entity-%d", i);
        handles[i] = mx_entity_create(w);
        mx_entity_set_name(w, handles[i], name);
    }
    double start = plat_time();
    for (int i = 0; i < N; i++) {
        char name[32];
        snprintf(name, sizeof(name), "entity-%d", i);
        MxEntity e = mx_entity_find(w, name);
        if (e.index != handles[i].index || e.gen != handles[i].gen) {
            CHECK(0);
            break;
        }
    }
    double elapsed = plat_time() - start;
    CHECK(elapsed < 0.1);
    MxEntity e = mx_entity_find(w, "entity-42");
    CHECK(strcmp(mx_entity_name(w, e), "entity-42") == 0);
    mx_entity_destroy(w, e);
    CHECK(mx_entity_find(w, "entity-42").index == MX_ENTITY_NONE.index);
    MxEntity again = mx_entity_create(w);
    mx_entity_set_name(w, again, "entity-42");
    CHECK(mx_entity_find(w, "entity-42").index == again.index);
    mx_mem_free(handles);
    mx_world_destroy(w);
}

static void test_components(void)
{
    MxWorld *w = mx_world_create();
    MxEntity e = mx_entity_create(w);
    CHECK(mx_comp_has(w, e, MX_COMP_TRANSFORM));
    CHECK(!mx_comp_has(w, e, MX_COMP_MESH));
    MxMeshRenderer *mesh = (MxMeshRenderer *)mx_comp_add(w, e, MX_COMP_MESH);
    CHECK(mesh != NULL);
    if (mesh) {
        mesh->mesh = 3;
        mesh->material = 2;
        mesh->tint = 0xFF112233u;
    }
    MxMeshRenderer *same = (MxMeshRenderer *)mx_comp_get(w, e, MX_COMP_MESH);
    CHECK(same == mesh);
    CHECK(same && same->mesh == 3 && same->tint == 0xFF112233u);
    MxCollider *col = (MxCollider *)mx_comp_add(w, e, MX_COMP_COLLIDER);
    CHECK(col != NULL);
    if (col) {
        CHECK(col->shape == 0 && col->radius == 0.0f);
        col->radius = 1.5f;
    }
    MxEntity other = mx_entity_create(w);
    MxCollider *oc = (MxCollider *)mx_comp_add(w, other, MX_COMP_COLLIDER);
    CHECK(oc != NULL && oc != col);
    if (oc) oc->radius = 2.5f;
    mx_comp_remove(w, e, MX_COMP_MESH);
    CHECK(!mx_comp_has(w, e, MX_COMP_MESH));
    CHECK(mx_comp_get(w, e, MX_COMP_MESH) == NULL);
    mx_comp_remove(w, e, MX_COMP_COLLIDER);
    CHECK(!mx_comp_has(w, e, MX_COMP_COLLIDER));
    MxCollider *after = (MxCollider *)mx_comp_get(w, other, MX_COMP_COLLIDER);
    CHECK(after && after->radius == 2.5f);
    mx_comp_remove(w, e, MX_COMP_TRANSFORM);
    CHECK(mx_comp_has(w, e, MX_COMP_TRANSFORM));
    mx_world_destroy(w);
}

static void test_hierarchy(void)
{
    MxWorld *w = mx_world_create();
    MxEntity parent = mx_entity_create(w);
    MxEntity child = mx_entity_create(w);
    MxTransform *pt = (MxTransform *)mx_comp_get(w, parent, MX_COMP_TRANSFORM);
    MxTransform *ct = (MxTransform *)mx_comp_get(w, child, MX_COMP_TRANSFORM);
    CHECK(pt && ct);
    if (!pt || !ct) {
        mx_world_destroy(w);
        return;
    }
    pt->position = mx_v3(10.0f, 0.0f, 0.0f);
    ct->position = mx_v3(1.0f, 2.0f, 3.0f);
    mx_entity_set_parent(w, child, parent);
    CHECK(mx_entity_parent(w, child).index == parent.index);
    mx_world_tick(w, 1.0f / 60.0f);
    MxV3 world = mx_mat4_mul_point(&ct->world, mx_v3(0.0f, 0.0f, 0.0f));
    CHECK_NEAR(world.x, 11.0f, 1e-4f);
    CHECK_NEAR(world.y, 2.0f, 1e-4f);
    CHECK_NEAR(world.z, 3.0f, 1e-4f);
    MxEntity grandchild = mx_entity_create(w);
    MxTransform *gt = (MxTransform *)mx_comp_get(w, grandchild, MX_COMP_TRANSFORM);
    CHECK(gt != NULL);
    if (gt) {
        gt->position = mx_v3(0.0f, 1.0f, 0.0f);
        mx_entity_set_parent(w, grandchild, child);
    }
    mx_world_tick(w, 1.0f / 60.0f);
    if (gt) {
        world = mx_mat4_mul_point(&gt->world, mx_v3(0.0f, 0.0f, 0.0f));
        CHECK_NEAR(world.x, 11.0f, 1e-4f);
        CHECK_NEAR(world.y, 3.0f, 1e-4f);
    }
    pt->position = mx_v3(20.0f, 0.0f, 0.0f);
    mx_world_tick(w, 1.0f / 60.0f);
    world = mx_mat4_mul_point(&ct->world, mx_v3(0.0f, 0.0f, 0.0f));
    CHECK_NEAR(world.x, 21.0f, 1e-4f);
    CHECK_NEAR(ct->prev_position.x, 1.0f, 1e-4f);

    MxEntity rotated = mx_entity_create(w);
    MxTransform *rt = (MxTransform *)mx_comp_get(w, rotated, MX_COMP_TRANSFORM);
    MxEntity tip = mx_entity_create(w);
    MxTransform *tt = (MxTransform *)mx_comp_get(w, tip, MX_COMP_TRANSFORM);
    CHECK(rt && tt);
    if (rt && tt) {
        rt->position = mx_v3(0.0f, 0.0f, 0.0f);
        rt->rotation = mx_quat_axis_angle(mx_v3(0.0f, 0.0f, 1.0f), MX_PI * 0.5f);
        tt->position = mx_v3(1.0f, 0.0f, 0.0f);
        mx_entity_set_parent(w, tip, rotated);
        mx_world_tick(w, 1.0f / 60.0f);
        world = mx_mat4_mul_point(&tt->world, mx_v3(0.0f, 0.0f, 0.0f));
        CHECK_NEAR(world.x, 0.0f, 1e-4f);
        CHECK_NEAR(world.y, 1.0f, 1e-4f);
    }

    mx_entity_destroy(w, parent);
    mx_world_tick(w, 1.0f / 60.0f);
    world = mx_mat4_mul_point(&ct->world, mx_v3(0.0f, 0.0f, 0.0f));
    CHECK_NEAR(world.x, 1.0f, 1e-4f);
    mx_world_destroy(w);
}

static void test_instantiate(void)
{
    const char *text = "{\"kind\":\"3d\",\"objects\":["
        "{\"id\":\"ground\",\"type\":\"plane\",\"pos\":[0,0,0],\"scale\":[24,1,24],\"color\":\"#6E7277\"},"
        "{\"id\":\"player\",\"type\":\"box\",\"pos\":[1,2,3],\"rot\":[0,90,0],\"color\":\"#4C8DDB\"},"
        "{\"id\":\"ball\",\"type\":\"sphere\",\"pos\":[2,1,4],\"scale\":[1.5,1.5,1.5]},"
        "{\"id\":\"ship\",\"type\":\"model\",\"model\":\"assets/ship.mcm\"},"
        "{\"id\":\"lamp\",\"type\":\"light\",\"pos\":[2,4,2],\"color\":\"#FFF2D8\"},"
        "{\"id\":\"title\",\"type\":\"text\",\"rect\":[10,20,300,16],\"color\":\"#F2F3F5\",\"text\":\"hello\"}"
        "]}";
    char err[128];
    Json *doc = json_parse(text, err, sizeof(err));
    CHECK(doc != NULL);
    if (!doc) return;
    MxWorld *w = mx_world_from_scene(doc);
    CHECK(w != NULL && mx_entity_count(w) == 6);
    if (!w) {
        json_free(doc);
        return;
    }
    MxEntity ground = mx_entity_find(w, "ground");
    MxEntity player = mx_entity_find(w, "player");
    MxEntity ship = mx_entity_find(w, "ship");
    MxEntity lamp = mx_entity_find(w, "lamp");
    MxEntity title = mx_entity_find(w, "title");
    CHECK(mx_entity_alive(w, ground));
    CHECK(mx_entity_alive(w, player));
    CHECK(mx_entity_alive(w, ship));
    CHECK(mx_entity_alive(w, lamp));
    CHECK(mx_entity_alive(w, title));

    MxTransform *pt = (MxTransform *)mx_comp_get(w, player, MX_COMP_TRANSFORM);
    CHECK(pt != NULL);
    if (pt) {
        CHECK_NEAR(pt->position.x, 1.0f, 1e-5f);
        CHECK_NEAR(pt->position.y, 2.0f, 1e-5f);
        CHECK_NEAR(pt->position.z, 3.0f, 1e-5f);
        MxV3 fwd = mx_quat_rotate(pt->rotation, mx_v3(1.0f, 0.0f, 0.0f));
        CHECK_NEAR(fwd.x, 0.0f, 1e-5f);
        CHECK_NEAR(fwd.z, -1.0f, 1e-5f);
    }
    MxMeshRenderer *pm = (MxMeshRenderer *)mx_comp_get(w, player, MX_COMP_MESH);
    CHECK(pm && pm->mesh == MX_MESH_BOX && pm->tint == 0xFFDB8D4Cu);
    MxMeshRenderer *gm = (MxMeshRenderer *)mx_comp_get(w, ground, MX_COMP_MESH);
    CHECK(gm && gm->mesh == MX_MESH_PLANE);
    MxMeshRenderer *sm = (MxMeshRenderer *)mx_comp_get(w, ship, MX_COMP_MESH);
    CHECK(sm && sm->mesh == MX_MESH_MODEL && sm->asset && strcmp(sm->asset, "assets/ship.mcm") == 0);
    MxLight *light = (MxLight *)mx_comp_get(w, lamp, MX_COMP_LIGHT);
    CHECK(light != NULL);
    if (light) {
        CHECK_NEAR(light->color.x, 1.0f, 0.01f);
        CHECK_NEAR(light->color.y, 0.949f, 0.01f);
    }
    MxSprite *sprite = (MxSprite *)mx_comp_get(w, title, MX_COMP_SPRITE);
    CHECK(sprite && sprite->kind == MX_SPRITE_TEXT);
    if (sprite) {
        CHECK(sprite->text && strcmp(sprite->text, "hello") == 0);
        CHECK_NEAR(sprite->rect[2], 300.0f, 1e-5f);
    }

    Json *out = mx_world_to_json(w);
    CHECK(out != NULL);
    char *written = out ? json_write(out, 0) : NULL;
    CHECK(written != NULL);
    Json *again = written ? json_parse(written, err, sizeof(err)) : NULL;
    CHECK(again != NULL);
    if (again) {
        MxWorld *w2 = mx_world_from_scene(again);
        CHECK(w2 && mx_entity_count(w2) == 6);
        MxEntity p2 = mx_entity_find(w2, "player");
        MxTransform *t2 = (MxTransform *)mx_comp_get(w2, p2, MX_COMP_TRANSFORM);
        CHECK(t2 != NULL);
        if (t2) {
            CHECK_NEAR(t2->position.x, 1.0f, 1e-5f);
            CHECK_NEAR(t2->position.y, 2.0f, 1e-5f);
            CHECK_NEAR(t2->position.z, 3.0f, 1e-5f);
            MxV3 fwd = mx_quat_rotate(t2->rotation, mx_v3(1.0f, 0.0f, 0.0f));
            CHECK_NEAR(fwd.z, -1.0f, 1e-3f);
        }
        MxSprite *s2 = (MxSprite *)mx_comp_get(w2, mx_entity_find(w2, "title"), MX_COMP_SPRITE);
        CHECK(s2 && s2->text && strcmp(s2->text, "hello") == 0);
        CHECK(mx_comp_get(w2, mx_entity_find(w2, "lamp"), MX_COMP_LIGHT) != NULL);
        mx_world_destroy(w2);
    }
    json_free(again);
    free(written);
    json_free(out);
    mx_world_destroy(w);
    json_free(doc);

    if (plat_file_exists("samples/hello/mexnya/scenes/main.json")) {
        Json *sample = json_load("samples/hello/mexnya/scenes/main.json", err, sizeof(err));
        CHECK(sample != NULL);
        if (sample) {
            MxWorld *sw = mx_world_from_scene(sample);
            CHECK(sw && mx_entity_count(sw) == 6);
            MxTransform *st = (MxTransform *)mx_comp_get(sw, mx_entity_find(sw, "player"), MX_COMP_TRANSFORM);
            CHECK(st && st->position.y == 0.5f);
            CHECK(mx_comp_get(sw, mx_entity_find(sw, "light"), MX_COMP_LIGHT) != NULL);
            mx_world_destroy(sw);
            json_free(sample);
        }
    }
}

int main(void)
{
#ifdef MX_MEM_TRACK
    size_t base = mx_mem_live_blocks();
#endif
    test_entities();
    test_names();
    test_components();
    test_hierarchy();
    test_instantiate();
    mx_str_intern_shutdown();
#ifdef MX_MEM_TRACK
    CHECK(mx_mem_live_blocks() == base);
    if (mx_mem_live_blocks() != base) mx_mem_report();
#endif
    if (s_failures) {
        printf("world_test: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("world_test: all checks passed\n");
    return 0;
}
