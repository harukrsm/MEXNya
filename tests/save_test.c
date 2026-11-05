#include "engine/game/save.h"
#include "engine/game/settings.h"
#include "engine/world/components.h"
#include "engine/core/str.h"
#include "engine/platform/platform.h"

#include <pthread.h>
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

static MxEntity named(MxWorld *w, const char *name) { return mx_entity_find(w, name); }

static void fill_components(MxWorld *w, MxEntity e)
{
    MxMeshRenderer *m = (MxMeshRenderer *)mx_comp_add(w, e, MX_COMP_MESH);
    m->mesh = MX_MESH_MODEL;
    m->material = 4;
    m->tint = 0x11223344u;
    m->layer = 2;
    m->asset = mx_str_intern("assets/x.mcm");

    MxCollider *c = (MxCollider *)mx_comp_add(w, e, MX_COMP_COLLIDER);
    c->shape = 1;
    c->half_extents = mx_v3(1.0f, 2.0f, 3.0f);
    c->radius = 0.5f;
    c->layer = 3;
    c->mask = 7;
    c->trigger = 1;

    MxRigidBody *rb = (MxRigidBody *)mx_comp_add(w, e, MX_COMP_RIGIDBODY);
    rb->type = MX_BODY_DYNAMIC;
    rb->velocity = mx_v3(0.1f, 0.2f, 0.3f);
    rb->mass = 2.0f;
    rb->damping = 0.9f;
    rb->layer = 1;
    rb->mask = 2;

    MxScript *sc = (MxScript *)mx_comp_add(w, e, MX_COMP_SCRIPT);
    sc->source = mx_str_intern("lua/x.lua");
    sc->started = 1;

    MxAudioSource *au = (MxAudioSource *)mx_comp_add(w, e, MX_COMP_AUDIO);
    au->clip = mx_str_intern("sfx.wav");
    au->volume = 0.7f;
    au->loop = 1;
    au->playing = 1;

    MxWorldCamera *cam = (MxWorldCamera *)mx_comp_add(w, e, MX_COMP_CAMERA);
    cam->fov = 60.0f;
    cam->near_m = 0.1f;
    cam->far_m = 100.0f;

    MxLight *li = (MxLight *)mx_comp_add(w, e, MX_COMP_LIGHT);
    li->color = mx_v3(1.0f, 0.5f, 0.25f);
    li->intensity = 2.0f;
    li->range = 10.0f;

    MxSprite *sp = (MxSprite *)mx_comp_add(w, e, MX_COMP_SPRITE);
    sp->kind = MX_SPRITE_TEXT;
    sp->texture = 5;
    sp->color = 0xAABBCCDDu;
    sp->layer = 1;
    sp->rect[0] = 10.0f;
    sp->rect[1] = 20.0f;
    sp->rect[2] = 300.0f;
    sp->rect[3] = 16.0f;
    sp->text = mx_str_intern("hello");
    sp->image = mx_str_intern("ui.png");
}

static MxWorld *build_world(void)
{
    MxWorld *w = mx_world_create();
    MxEntity root = mx_entity_create(w);
    mx_entity_set_name(w, root, "root");
    mx_entity_set_type(w, root, "box");
    MxTransform *rt = (MxTransform *)mx_comp_get(w, root, MX_COMP_TRANSFORM);
    rt->position = mx_v3(1.0f, 2.0f, 3.0f);

    MxEntity player = mx_entity_create(w);
    mx_entity_set_name(w, player, "player");
    mx_entity_set_type(w, player, "model");
    MxTransform *pt = (MxTransform *)mx_comp_get(w, player, MX_COMP_TRANSFORM);
    pt->position = mx_v3(-1.5f, 0.25f, 8.0f);
    pt->rotation = mx_quat_axis_angle(mx_v3(0.0f, 1.0f, 0.0f), 1.25f);
    pt->scale = mx_v3(2.0f, 2.0f, 2.0f);
    mx_entity_set_parent(w, player, root);
    fill_components(w, player);

    MxEntity text = mx_entity_create(w);
    mx_entity_set_name(w, text, "title");
    mx_entity_set_type(w, text, "text");
    MxSprite *sp = (MxSprite *)mx_comp_add(w, text, MX_COMP_SPRITE);
    sp->kind = MX_SPRITE_TEXT;
    sp->text = mx_str_intern("saved");
    mx_entity_set_parent(w, text, player);
    mx_world_update_transforms(w);
    return w;
}

static void check_world(MxWorld *w)
{
    CHECK(mx_entity_count(w) == 3);
    MxEntity root = named(w, "root");
    MxEntity player = named(w, "player");
    MxEntity title = named(w, "title");
    CHECK(mx_entity_alive(w, root));
    CHECK(mx_entity_alive(w, player));
    CHECK(mx_entity_alive(w, title));
    CHECK(strcmp(mx_entity_type(w, player), "model") == 0);
    CHECK(mx_entity_parent(w, player).index == root.index);
    CHECK(mx_entity_parent(w, title).index == player.index);
    MxTransform *rt = (MxTransform *)mx_comp_get(w, root, MX_COMP_TRANSFORM);
    MxTransform *pt = (MxTransform *)mx_comp_get(w, player, MX_COMP_TRANSFORM);
    CHECK(rt && rt->position.x == 1.0f && rt->position.y == 2.0f && rt->position.z == 3.0f);
    CHECK(pt && pt->position.x == -1.5f && pt->position.y == 0.25f && pt->position.z == 8.0f);
    CHECK(pt && pt->scale.x == 2.0f);
    CHECK(pt && pt->rotation.y > 0.5f);
    MxMeshRenderer *m = (MxMeshRenderer *)mx_comp_get(w, player, MX_COMP_MESH);
    CHECK(m && m->mesh == MX_MESH_MODEL && m->material == 4 && m->tint == 0x11223344u && m->layer == 2);
    CHECK(m && m->asset && strcmp(m->asset, "assets/x.mcm") == 0);
    MxCollider *c = (MxCollider *)mx_comp_get(w, player, MX_COMP_COLLIDER);
    CHECK(c && c->shape == 1 && c->radius == 0.5f && c->layer == 3 && c->mask == 7 && c->trigger == 1);
    CHECK(c && c->half_extents.z == 3.0f);
    MxRigidBody *rb = (MxRigidBody *)mx_comp_get(w, player, MX_COMP_RIGIDBODY);
    CHECK(rb && rb->type == MX_BODY_DYNAMIC && rb->mass == 2.0f && rb->damping == 0.9f && rb->layer == 1);
    CHECK(rb && rb->velocity.y == 0.2f);
    MxScript *sc = (MxScript *)mx_comp_get(w, player, MX_COMP_SCRIPT);
    CHECK(sc && sc->started == 1 && strcmp(sc->source, "lua/x.lua") == 0);
    MxAudioSource *au = (MxAudioSource *)mx_comp_get(w, player, MX_COMP_AUDIO);
    CHECK(au && au->volume == 0.7f && au->loop == 1 && au->playing == 1 && strcmp(au->clip, "sfx.wav") == 0);
    MxWorldCamera *cam = (MxWorldCamera *)mx_comp_get(w, player, MX_COMP_CAMERA);
    CHECK(cam && cam->fov == 60.0f && cam->far_m == 100.0f);
    MxLight *li = (MxLight *)mx_comp_get(w, player, MX_COMP_LIGHT);
    CHECK(li && li->intensity == 2.0f && li->range == 10.0f && li->color.z == 0.25f);
    MxSprite *sp = (MxSprite *)mx_comp_get(w, player, MX_COMP_SPRITE);
    CHECK(sp && sp->texture == 5 && sp->color == 0xAABBCCDDu && sp->rect[2] == 300.0f);
    CHECK(sp && sp->text && strcmp(sp->text, "hello") == 0 && strcmp(sp->image, "ui.png") == 0);
    MxSprite *ts = (MxSprite *)mx_comp_get(w, title, MX_COMP_SPRITE);
    CHECK(ts && ts->text && strcmp(ts->text, "saved") == 0);
}

static int file_bytes(const char *path, unsigned char **out)
{
    int size = 0;
    unsigned char *data = (unsigned char *)plat_read_file(path, &size, 0);
    *out = data;
    return size;
}

typedef struct {
    const char *slot;
    const char *name;
    int ok;
} Writer;

static void *writer(void *arg)
{
    Writer *w = (Writer *)arg;
    MxWorld *world = mx_world_create();
    MxEntity e = mx_entity_create(world);
    mx_entity_set_name(world, e, w->name);
    for (int i = 0; i < 40; i++) w->ok &= mx_save_write(world, w->slot) == 0;
    mx_world_destroy(world);
    return NULL;
}

static void test_settings(const char *dir)
{
    char path[1200], err[128];
    snprintf(path, sizeof(path), "%s/settings.json", dir);
    Json *defaults = json_parse("{\"volume\":0.8,\"video\":{\"width\":1280,\"height\":720}}", err, sizeof(err));
    CHECK(defaults != NULL);
    if (!defaults) return;
    plat_remove_file(path);
    Json *s = mx_settings_load(path, defaults);
    CHECK(s != NULL);
    CHECK(s && json_getn(s, "volume", 0.0) == 0.8);
    CHECK(s && json_getn(json_get(s, "video"), "height", 0.0) == 720.0);
    CHECK(s && json_getn(s, "schema_version", 0.0) == MX_SETTINGS_VERSION);
    if (s) {
        json_setn(s, "volume", 0.5);
        CHECK(mx_settings_save(path, s) == 0);
        json_free(s);
    }
    s = mx_settings_load(path, defaults);
    CHECK(s && json_getn(s, "volume", 0.0) == 0.5);
    CHECK(s && json_getn(json_get(s, "video"), "width", 0.0) == 1280.0);
    json_free(s);
    json_free(defaults);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    plat_set_user_dir(dir);
    char path[1200];
    CHECK(mx_save_path(path, (int)sizeof(path), "slot1") == 0);

    MxWorld *w = build_world();
    CHECK(mx_save_write(w, "slot1") == 0);
    CHECK(mx_save_exists("slot1"));
    CHECK(mx_save_write(w, ".") != 0);
    CHECK(mx_save_write(w, "../evil") != 0);

    MxWorld *loaded = mx_world_create();
    CHECK(mx_save_read(loaded, "slot1") == 0);
    check_world(loaded);

    CHECK(mx_save_write(w, "slot2") == 0);
    unsigned char *a = NULL, *b = NULL;
    char path2[1200];
    mx_save_path(path, (int)sizeof(path), "slot1");
    mx_save_path(path2, (int)sizeof(path2), "slot2");
    int na = file_bytes(path, &a), nb = file_bytes(path2, &b);
    CHECK(na == nb && na > 0 && a && b && memcmp(a, b, (size_t)na) == 0);

    int before = mx_entity_count(loaded);
    unsigned char *bad = (unsigned char *)malloc((size_t)na);
    CHECK(bad != NULL);
    if (bad) {
        memcpy(bad, a, (size_t)na);
        bad[4] = 99;
        bad[5] = 0;
        bad[6] = 0;
        bad[7] = 0;
        mx_save_path(path2, (int)sizeof(path2), "badver");
        plat_write_file_atomic(path2, bad, na);
        CHECK(mx_save_read(loaded, "badver") != 0);
        CHECK(mx_entity_count(loaded) == before);

        memcpy(bad, a, (size_t)na);
        bad[na - 1] ^= 0xFFu;
        mx_save_path(path2, (int)sizeof(path2), "badcrc");
        plat_write_file_atomic(path2, bad, na);
        CHECK(mx_save_read(loaded, "badcrc") != 0);
        CHECK(mx_entity_count(loaded) == before);

        mx_save_path(path2, (int)sizeof(path2), "truncated");
        plat_write_file_atomic(path2, bad, na / 2);
        CHECK(mx_save_read(loaded, "truncated") != 0);
        CHECK(mx_entity_count(loaded) == before);
        free(bad);
    }

    Writer wa = {"shared", "writer-a", 1};
    Writer wb = {"shared", "writer-b", 1};
    pthread_t ta, tb;
    CHECK(pthread_create(&ta, NULL, writer, &wa) == 0);
    CHECK(pthread_create(&tb, NULL, writer, &wb) == 0);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    CHECK(wa.ok && wb.ok);
    MxWorld *shared = mx_world_create();
    CHECK(mx_save_read(shared, "shared") == 0);
    CHECK(mx_entity_count(shared) == 1);
    const char *winner = mx_entity_name(shared, mx_world_entity_at(shared, 0));
    CHECK(strcmp(winner, "writer-a") == 0 || strcmp(winner, "writer-b") == 0);

    CHECK(mx_save_delete("slot1") == 0);
    CHECK(!mx_save_exists("slot1"));
    CHECK(mx_save_read(loaded, "slot1") != 0);
    test_settings(dir);

    free(a);
    free(b);
    mx_world_destroy(shared);
    mx_world_destroy(loaded);
    mx_world_destroy(w);
    if (s_failures) {
        printf("save_test: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("save_test: all checks passed\n");
    return 0;
}
