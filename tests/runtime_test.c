// Engine runtime checks without a window: JSON, i18n and the scene runtime's Lines.
#include "engine/core/json.h"
#include "engine/core/i18n.h"
#include "engine/scene/scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int reached;
static void on_port(MxProject *p, const char *node, const char *port, void *user)
{
    (void)p; (void)user;
    if (!strcmp(node, "hud") && !strcmp(port, "show")) reached++;
}

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : ".";
    // JSON keeps key order and round-trips.
    Json *j = json_parse("{\"b\": 1, \"a\": [1, 2, {\"x\": \"y\"}], \"s\": \"\\u732b\"}", NULL, 0);
    CHECK(j && json_len(j) == 3);
    char *t = json_write(j, 0);
    CHECK(t && !strcmp(t, "{\"b\":1,\"a\":[1,2,{\"x\":\"y\"}],\"s\":\"\xe7\x8c\xab\"}"));
    free(t);
    CHECK(!strcmp(json_gets(json_path(j, "a.2"), "x", ""), "y"));
    json_free(j);
    // i18n: English fallback, switching back and forth, unknown keys return the key.
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/engine/resources/lang", root);
    CHECK(i18n_add_domain(dir, "editor") > 100);
    const char *saved[I18N_LANG_COUNT];
    char expected[I18N_LANG_COUNT][128];
    for (int i = 0; i < I18N_LANG_COUNT; i++) {
        i18n_set_language(i18n_lang_code(i));
        saved[i] = tr("top.save");
        snprintf(expected[i], sizeof(expected[i]), "%s", saved[i]);
    }
    const char *en = saved[0];
    for (int i = 0; i < 1000; i++) i18n_set_language(i18n_lang_code(i % I18N_LANG_COUNT));
    for (int i = 0; i < I18N_LANG_COUNT; i++) CHECK(!strcmp(saved[i], expected[i]));
    i18n_add_domain(dir, "editor");
    for (int i = 0; i < I18N_LANG_COUNT; i++) CHECK(!strcmp(saved[i], expected[i]));
    i18n_set_language("ko");
    CHECK(strcmp(tr("top.save"), "Save") != 0);
    i18n_set_language("en");
    CHECK(!strcmp(tr("top.save"), en));
    CHECK(!strcmp(tr("no.such.key"), "no.such.key"));
    CHECK(i18n_rejected() == 0);
    // Scene runtime: the sample's Line main.use -> hud.show.
    snprintf(dir, sizeof(dir), "%s/samples/hello/mexnya", root);
    MxProject *p = mx_project_load(dir);
    CHECK(p != NULL);
    if (p) {
        int hud = mx_scene_find(p, "hud");
        CHECK(hud >= 0 && !mx_scene_visible(p, hud));
        mx_on_port(p, on_port, NULL);
        CHECK(mx_emit(p, "main.use") == 1);
        CHECK(reached == 1 && mx_scene_visible(p, hud));
        CHECK(mx_object(p, mx_scene_find(p, "main"), "player") != NULL);
        mx_project_free(p);
    }
    printf(fails ? "runtime test: %d failed\n" : "runtime test OK\n", fails);
    return fails ? 1 : 0;
}
