// The project on disk:
//   mexnya/project.json        name, scene order (layers), tunnels, lines, graph layout
//   mexnya/scenes/<id>.json    one scene: kind, camera, objects, notes
//   mexnya/notes/<scene>/      snapshots of notes (PNG) for people and agents
//   mexnya/NOTES.md            every note in plain text, regenerated on save
//   mexnya/agent/              inbox/outbox for agents, log.jsonl of every operation
//   mexnya/plugins/<name>/     plugins (plugin.json + Lua and/or a native library)
#include "ed.h"
#include "engine/platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Ed ed;

static unsigned int s_hash[ED_MAX_SCENES + 1];

void ed_path(char *out, int size, const char *rel)
{
    snprintf(out, (size_t)size, "%s/%s", ed.dir, rel ? rel : "");
}

void ed_now_iso(char *out, int size)
{
    time_t t = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(out, (size_t)size, "%Y-%m-%dT%H:%M:%S", &tmv);
}

unsigned int parse_color(const char *s, unsigned int def)
{
    if (!s) return def;
    if (*s == '#') s++;
    char *end;
    unsigned long v = strtoul(s, &end, 16);
    return end - s == 6 ? (unsigned int)v : def;
}

void format_color(char *out, int size, unsigned int rgb) { snprintf(out, (size_t)size, "#%06X", rgb & 0xFFFFFF); }

void cam_default(Cam *c)
{
    c->target[0] = 0; c->target[1] = 0.5f; c->target[2] = 0;
    c->yaw = 35; c->pitch = 28; c->dist = 12; c->fov = 55; c->ortho = 0;
}

void cam_from_json(Cam *c, const Json *j)
{
    cam_default(c);
    if (!j) return;
    json_getv(j, "target", c->target, 3);
    c->yaw = (float)json_getn(j, "yaw", c->yaw);
    c->pitch = (float)json_getn(j, "pitch", c->pitch);
    c->dist = (float)json_getn(j, "dist", c->dist);
    c->fov = (float)json_getn(j, "fov", c->fov);
    c->ortho = json_getb(j, "ortho", 0);
    if (c->dist < 0.1f) c->dist = 12;
    if (c->fov < 5 || c->fov > 150) c->fov = 55;
}

Json *cam_to_json(const Cam *c)
{
    Json *j = json_obj();
    json_setv(j, "target", c->target, 3);
    json_setn(j, "yaw", (double)(int)(c->yaw * 100) / 100.0);
    json_setn(j, "pitch", (double)(int)(c->pitch * 100) / 100.0);
    json_setn(j, "dist", (double)(int)(c->dist * 100) / 100.0);
    json_setn(j, "fov", c->fov);
    json_setb(j, "ortho", c->ortho);
    return j;
}

Json *scene_at(int i) { return i >= 0 && i < ed.nscenes ? ed.scenes[i] : NULL; }
const char *scene_id(const Json *s) { return json_gets(s, "id", ""); }
int scene_is_2d(const Json *s) { return !strcmp(json_gets(s, "kind", "3d"), "2d"); }
Json *cur_scene(void) { return scene_at(ed.cur); }

int scene_index(const char *id)
{
    for (int i = 0; id && i < ed.nscenes; i++)
        if (!strcmp(scene_id(ed.scenes[i]), id)) return i;
    return -1;
}

Json *scene_by_id(const char *id) { return scene_at(scene_index(id)); }

static Json *ensure_arr(Json *obj, const char *key)
{
    Json *a = json_get(obj, key);
    if (!a || a->type != JSON_ARR) a = json_set(obj, key, json_arr());
    return a;
}

Json *scene_objects(Json *s) { return s ? ensure_arr(s, "objects") : NULL; }
Json *scene_notes(Json *s) { return s ? ensure_arr(s, "notes") : NULL; }
Json *find_object(Json *s, const char *id) { return s && id && id[0] ? json_find_id(scene_objects(s), id) : NULL; }
Json *find_note(Json *s, const char *id) { return s && id && id[0] ? json_find_id(scene_notes(s), id) : NULL; }

void make_id(char *out, int size, const char *prefix, const Json *taken_in)
{
    for (int n = 1; n < 100000; n++) {
        snprintf(out, (size_t)size, "%s_%d", prefix, n);
        if (taken_in && json_find_id(taken_in, out)) continue;
        if (!taken_in && scene_index(out) >= 0) continue;
        if (!taken_in && json_find_id(json_get(ed.project, "tunnels"), out)) continue;
        return;
    }
}

static unsigned int hash_text(const char *s, int n)
{
    unsigned int h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}

static unsigned int file_hash(const char *path)
{
    int size = 0;
    char *b = (char *)plat_read_file(path, &size, 0);
    if (!b) return 0;
    unsigned int h = hash_text(b, size);
    free(b);
    return h;
}

// ---------------------------------------------------------------- starter project
static Json *obj3(const char *id, const char *name, const char *type, float x, float y, float z,
                  float sx, float sy, float sz, const char *color)
{
    Json *o = json_obj();
    json_sets(o, "id", id);
    json_sets(o, "name", name);
    json_sets(o, "type", type);
    float p[3] = {x, y, z}, r[3] = {0, 0, 0}, s[3] = {sx, sy, sz};
    json_setv(o, "pos", p, 3);
    json_setv(o, "rot", r, 3);
    json_setv(o, "scale", s, 3);
    json_sets(o, "color", color);
    return o;
}

static Json *ports(const char *const *names, const char *const *dirs, int n)
{
    Json *a = json_arr();
    for (int i = 0; i < n; i++) {
        Json *p = json_obj();
        json_sets(p, "name", names[i]);
        json_sets(p, "dir", dirs[i]);
        json_push(a, p);
    }
    return a;
}

Json *scene_new(const char *id, const char *name, const char *kind)
{
    Json *s = json_obj();
    json_sets(s, "format", "mexnya-scene");
    json_setn(s, "version", 1);
    json_sets(s, "id", id);
    json_sets(s, "name", name);
    json_sets(s, "kind", kind);
    json_setb(s, "visible", 1);
    json_setb(s, "locked", 0);
    Json *set = json_set(s, "settings", json_obj());
    if (!strcmp(kind, "2d")) {
        float c[2] = {1280, 720};
        json_setv(set, "canvas", c, 2);
        json_sets(set, "background", "");
    } else {
        json_sets(set, "background", "#3A3D42");
        float fog[2] = {20, 80};
        json_setv(set, "fog", fog, 2);
        json_sets(set, "fog_color", "#808890");
        json_setb(set, "fog_enabled", 0);
    }
    static const char *const IN_OUT[] = {"show", "hide", "enter", "exit"};
    static const char *const DIRS[] = {"in", "in", "out", "out"};
    json_set(s, "ports", ports(IN_OUT, DIRS, 4));
    if (strcmp(kind, "2d") != 0) {
        Cam c;
        cam_default(&c);
        json_set(s, "camera", cam_to_json(&c));
    }
    json_set(s, "objects", json_arr());
    json_set(s, "notes", json_arr());
    return s;
}

static void starter(const char *name)
{
    Json *p = json_obj();
    json_sets(p, "format", "mexnya-project");
    json_setn(p, "version", 1);
    json_sets(p, "name", name);
    Json *order = json_set(p, "scenes", json_arr());
    json_push(order, json_str("main"));
    json_set(p, "tunnels", json_arr());
    json_set(p, "lines", json_arr());
    Json *graph = json_set(p, "graph", json_obj());
    float gp[2] = {60, 60};
    json_setv(graph, "main", gp, 2);
    json_set(p, "plugins", json_obj());
    ed.project = p;
    Json *w = scene_new("main", "Main", "3d");
    Json *wo = scene_objects(w);
    json_push(wo, obj3("ground", "Ground", "plane", 0, 0, 0, 20, 1, 20, "#6E7277"));
    json_push(wo, obj3("light", "Light", "light", 2, 4, 2, 1, 1, 1, "#FFF2D8"));
    ed.scenes[0] = w;
    ed.nscenes = 1;
    cam_default(&ed.cams[0]);
}

// ---------------------------------------------------------------- open/save
static void scene_file(char *out, int size, const char *id)
{
    char rel[200];
    snprintf(rel, sizeof(rel), "scenes/%s.json", id);
    ed_path(out, size, rel);
}

static void fix_scene(Json *s, const char *id)
{
    if (!json_get(s, "id")) json_sets(s, "id", id);
    if (!json_get(s, "kind")) json_sets(s, "kind", "3d");
    if (!json_get(s, "name")) json_sets(s, "name", id);
    if (!json_get(s, "visible")) json_setb(s, "visible", 1);
    if (!json_get(s, "settings")) json_set(s, "settings", json_obj());
    if (!json_get(s, "ports")) json_set(s, "ports", json_arr());
    scene_objects(s);
    scene_notes(s);
}

static int load_all(void)
{
    char path[ED_PATH], err[256];
    ed_path(path, sizeof(path), "project.json");
    Json *p = json_load(path, err, sizeof(err));
    if (!p) { ed_log(LOG_ERR, "project.json: %s", err); return -1; }
    for (int i = 0; i < ed.nscenes; i++) json_free(ed.scenes[i]);
    ed.nscenes = 0;
    json_free(ed.project);
    ed.project = p;
    s_hash[ED_MAX_SCENES] = file_hash(path);
    Json *order = json_get(p, "scenes");
    for (Json *c = order ? order->child : NULL; c && ed.nscenes < ED_MAX_SCENES; c = c->next) {
        if (c->type != JSON_STR) continue;
        scene_file(path, sizeof(path), c->str);
        Json *s = json_load(path, err, sizeof(err));
        if (!s) { ed_log(LOG_WARN, "scene %s: %s", c->str, err); s = scene_new(c->str, c->str, "3d"); }
        fix_scene(s, c->str);
        s_hash[ed.nscenes] = file_hash(path);
        ed.scenes[ed.nscenes++] = s;
    }
    if (!json_get(p, "tunnels")) json_set(p, "tunnels", json_arr());
    if (!json_get(p, "lines")) json_set(p, "lines", json_arr());
    if (!json_get(p, "graph")) json_set(p, "graph", json_obj());
    for (int i = 0; i < ed.nscenes; i++) cam_from_json(&ed.cams[i], json_get(ed.scenes[i], "camera"));
    if (ed.cur >= ed.nscenes) ed.cur = ed.nscenes - 1;
    if (ed.cur < 0 && ed.nscenes) ed.cur = 0;
    return 0;
}

int project_reload(void)
{
    char keep[64];
    snprintf(keep, sizeof(keep), "%s", scene_id(cur_scene()));
    if (load_all() != 0) return -1;
    int i = scene_index(keep);
    if (i >= 0) ed.cur = i;
    ed.dirty = 0;
    return 0;
}

int project_open(const char *root)
{
    snprintf(ed.root, sizeof(ed.root), "%s", root);
    snprintf(ed.dir, sizeof(ed.dir), "%s/mexnya", root);
    char path[ED_PATH];
    ed_path(path, sizeof(path), "project.json");
    ed.cur = 0;
    if (!plat_file_exists(path)) {
        char name[128] = "MEXNya Project";
        char cfg[ED_PATH];
        snprintf(cfg, sizeof(cfg), "%s/mexnya.json", root);
        Json *c = json_load(cfg, NULL, 0);
        if (c) { snprintf(name, sizeof(name), "%s", json_gets(c, "name", name)); json_free(c); }
        plat_mkdirs(ed.dir);
        starter(name);
        project_save();
        ed_log(LOG_OK, "created a starter project in mexnya/");
    }
    return load_all();
}

int project_save(void)
{
    char path[ED_PATH];
    ed_path(path, sizeof(path), "scenes");
    plat_mkdirs(path);
    // Scene order follows the in-memory layer list.
    Json *order = json_set(ed.project, "scenes", json_arr());
    for (int i = 0; i < ed.nscenes; i++) {
        json_push(order, json_str(scene_id(ed.scenes[i])));
        Json *s = ed.scenes[i];
        if (!scene_is_2d(s)) json_set(s, "camera", cam_to_json(&ed.cams[i]));
        scene_file(path, sizeof(path), scene_id(s));
        char *t = json_write(s, 2);
        unsigned int h = t ? hash_text(t, (int)strlen(t)) : 0;
        free(t);
        if (h != s_hash[i]) {
            if (json_save(path, s) != 0) { ed_log(LOG_ERR, "cannot write %s", path); return -1; }
            s_hash[i] = h;
        }
    }
    ed_path(path, sizeof(path), "project.json");
    if (json_save(path, ed.project) != 0) { ed_log(LOG_ERR, "cannot write %s", path); return -1; }
    s_hash[ED_MAX_SCENES] = file_hash(path);
    notes_export_md();
    ed.dirty = 0;
    return 0;
}

// Files edited outside the editor (an agent writing JSON directly) are picked up.
int project_changed_on_disk(void)
{
    char path[ED_PATH];
    ed_path(path, sizeof(path), "project.json");
    if (file_hash(path) != s_hash[ED_MAX_SCENES]) return 1;
    for (int i = 0; i < ed.nscenes; i++) {
        scene_file(path, sizeof(path), scene_id(ed.scenes[i]));
        if (file_hash(path) != s_hash[i]) return 1;
    }
    return 0;
}

void project_close(void)
{
    for (int i = 0; i < ed.nscenes; i++) json_free(ed.scenes[i]);
    ed.nscenes = 0;
    json_free(ed.project);
    ed.project = NULL;
}

// ---------------------------------------------------------------- NOTES.md
static void shape_text(char *out, int size, const Json *n)
{
    const Json *sh = json_get(n, "shape");
    char *t = sh ? json_write(sh, 0) : NULL;
    snprintf(out, (size_t)size, "%s", t ? t : "{}");
    free(t);
}

void notes_export_md(void)
{
    char path[ED_PATH];
    ed_path(path, sizeof(path), "NOTES.md");
    size_t cap = 1 << 16, n = 0;
    char *b = (char *)malloc(cap);
    if (!b) return;
#define PUT(...) do { \
        int k_ = snprintf(b + n, cap - n, __VA_ARGS__); \
        if (k_ > 0 && n + (size_t)k_ + 1 >= cap) { cap = (cap + (size_t)k_) * 2; char *nb_ = (char *)realloc(b, cap); if (!nb_) { free(b); return; } b = nb_; k_ = snprintf(b + n, cap - n, __VA_ARGS__); } \
        if (k_ > 0) n += (size_t)k_; } while (0)
    PUT("# Notes (%s)\n\n", json_gets(ed.project, "name", "project"));
    PUT("Generated by MEXNya Editor from mexnya/scenes/*.json. Do not edit by hand: change notes\n"
        "with the editor or with operations (`mexnya agent note.set ...`).\n\n"
        "Coordinates: 2D scenes use canvas pixels (origin top-left). 3D notes are drawn on\n"
        "the viewport at the saved camera; `world` is where the note's centre hits the scene,\n"
        "`targets` are the objects inside the shape. Each note has a snapshot PNG.\n\n");
    int total = 0;
    for (int i = 0; i < ed.nscenes; i++) {
        Json *s = ed.scenes[i];
        Json *notes = json_get(s, "notes");
        if (!json_len(notes)) continue;
        PUT("## %s `%s` (%s)\n\n", json_gets(s, "name", ""), scene_id(s), json_gets(s, "kind", "3d"));
        int k = 0;
        for (Json *c = notes->child; c; c = c->next) {
            char shape[512];
            shape_text(shape, sizeof(shape), c);
            k++;
            total++;
            PUT("### %d. [%s] %s `%s`\n\n", k, json_gets(c, "status", "open"),
                json_gets(c, "kind", "box"), json_gets(c, "id", ""));
            PUT("- text: %s\n", json_gets(c, "text", ""));
            PUT("- by: %s at %s\n", json_gets(c, "author", "?"), json_gets(c, "created", ""));
            PUT("- shape (%s px): %s\n", scene_is_2d(s) ? "canvas" : "viewport", shape);
            Json *wv = json_get(c, "world");
            if (wv && wv->type == JSON_ARR) {
                char *t = json_write(wv, 0);
                PUT("- world: %s\n", t ? t : "");
                free(t);
            }
            Json *tg = json_get(c, "targets");
            if (json_len(tg)) {
                PUT("- targets:");
                for (Json *t = tg->child; t; t = t->next) {
                    Json *o = find_object(s, t->str);
                    PUT(" `%s` (%s)", t->str ? t->str : "", o ? json_gets(o, "name", "") : "missing");
                }
                PUT("\n");
            }
            const char *img = json_gets(c, "image", NULL);
            if (img) PUT("- image: [%s](%s)\n", img, img);
            Json *rep = json_get(c, "replies");
            for (Json *r = rep ? rep->child : NULL; r; r = r->next)
                PUT("- reply (%s): %s\n", json_gets(r, "author", "?"), json_gets(r, "text", ""));
            PUT("\n");
        }
    }
    if (!total) PUT("No notes yet.\n");
#undef PUT
    plat_write_file(path, b, (int)n);
    free(b);
}
