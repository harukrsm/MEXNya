// The operation layer: the one door into the project.
//
// The GUI, Lua scripts, plugins and outside agents all call these named operations
// with JSON arguments and get JSON back. Every call is logged (who, what, result) to
// mexnya/agent/log.jsonl; every change can be undone. Agents need no special rights:
// whatever an operation allows, any caller may do, and the log keeps the record.
#include "ed.h"
#include "engine/platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Json *scene_new(const char *id, const char *name, const char *kind);
int project_reload(void);

typedef struct {
    const char *name;
    const char *help;
    OpFn fn;
    int mutates;
    void *user;
} Op;

#define MAX_OPS 160
static Op s_ops[MAX_OPS];
static int s_nops;
static char s_plugin_names[64][64];
static char s_plugin_help[64][200];
static int s_nplugin;

// ---------------------------------------------------------------- results
static Json *ok(void)
{
    Json *r = json_obj();
    json_setb(r, "ok", 1);
    return r;
}

static Json *fail(const char *fmt, const char *a)
{
    char buf[300];
    snprintf(buf, sizeof(buf), fmt, a ? a : "");
    Json *r = json_obj();
    json_setb(r, "ok", 0);
    json_sets(r, "error", buf);
    return r;
}

// ---------------------------------------------------------------- undo
typedef struct {
    Json *project;
    Json *scenes[ED_MAX_SCENES];
    Cam cams[ED_MAX_SCENES];
    int n, cur;
    char label[64];
} State;

#define UNDO_MAX 80
static State s_undo[UNDO_MAX], s_redo[UNDO_MAX];
static int s_nundo, s_nredo;
static int s_group, s_group_captured;

static void state_free(State *s)
{
    json_free(s->project);
    for (int i = 0; i < s->n; i++) json_free(s->scenes[i]);
    memset(s, 0, sizeof(*s));
}

static void state_capture(State *s, const char *label)
{
    s->project = json_clone(ed.project);
    s->n = ed.nscenes;
    for (int i = 0; i < ed.nscenes; i++) s->scenes[i] = json_clone(ed.scenes[i]);
    memcpy(s->cams, ed.cams, sizeof(ed.cams));
    s->cur = ed.cur;
    snprintf(s->label, sizeof(s->label), "%s", label);
}

static void state_restore(State *s)
{
    // Takes ownership of s's documents.
    json_free(ed.project);
    for (int i = 0; i < ed.nscenes; i++) json_free(ed.scenes[i]);
    ed.project = s->project;
    ed.nscenes = s->n;
    for (int i = 0; i < s->n; i++) ed.scenes[i] = s->scenes[i];
    memcpy(ed.cams, s->cams, sizeof(ed.cams));
    ed.cur = s->cur < ed.nscenes ? s->cur : ed.nscenes - 1;
    memset(s, 0, sizeof(*s));
    ed.dirty = 1;
}

static void push(State *stack, int *n, State *s)
{
    if (*n == UNDO_MAX) {
        state_free(&stack[0]);
        memmove(stack, stack + 1, sizeof(State) * (UNDO_MAX - 1));
        (*n)--;
    }
    stack[(*n)++] = *s;
}

static void before_change(const char *label)
{
    if (s_group && s_group_captured) return;
    State s;
    state_capture(&s, label);
    push(s_undo, &s_nundo, &s);
    for (int i = 0; i < s_nredo; i++) state_free(&s_redo[i]);
    s_nredo = 0;
    if (s_group) s_group_captured = 1;
}

// A group (a slider or number being dragged) is one undo step and one log line: the
// log keeps the last request of the group, written when the group ends.
static Json *s_group_req, *s_group_res;
static char s_group_who[64];
static double s_group_ms;

void ops_group_begin(void)
{
    if (s_group) return;
    s_group = 1;
    s_group_captured = 0;
}

void ops_group_end(void)
{
    if (!s_group) return;
    s_group = 0;
    s_group_captured = 0;
    if (s_group_req) log_op(s_group_who, s_group_req, s_group_res, s_group_ms);
    json_free(s_group_req);
    json_free(s_group_res);
    s_group_req = s_group_res = NULL;
}

int ops_in_group(void) { return s_group; }
int ops_can_undo(void) { return s_nundo > 0; }
int ops_can_redo(void) { return s_nredo > 0; }

void ops_undo(void)
{
    if (!s_nundo) return;
    State now;
    state_capture(&now, s_undo[s_nundo - 1].label);
    push(s_redo, &s_nredo, &now);
    State *s = &s_undo[--s_nundo];
    ed_status(0xB4BCC2, T("status.undo"), s->label);
    state_restore(s);
}

void ops_redo(void)
{
    if (!s_nredo) return;
    State now;
    state_capture(&now, s_redo[s_nredo - 1].label);
    push(s_undo, &s_nundo, &now);
    State *s = &s_redo[--s_nredo];
    ed_status(0xB4BCC2, T("status.redo"), s->label);
    state_restore(s);
}

// ---------------------------------------------------------------- helpers
static Json *arg_scene(Json *args, int *index)
{
    const char *id = json_gets(args, "scene", NULL);
    int i = id ? scene_index(id) : ed.cur;
    if (index) *index = i;
    return scene_at(i);
}

static void merge(Json *dst, const Json *fields, int protect_id)
{
    for (const Json *f = fields ? fields->child : NULL; f; f = f->next) {
        if (!f->key) continue;
        if (protect_id && !strcmp(f->key, "id")) continue;
        if (f->type == JSON_NULL) { json_remove(dst, f->key); continue; }
        json_set(dst, f->key, json_clone(f));
    }
}

static Json *fields_of(Json *args)
{
    // object.set {id, fields:{...}} or flat {id, name: ..., pos: ...}
    Json *f = json_get(args, "fields");
    if (f && f->type == JSON_OBJ) return json_clone(f);
    Json *c = json_clone(args);
    json_remove(c, "scene");
    json_remove(c, "id");
    return c;
}

static int is_human(const char *w) { return w && !strcmp(w, "human"); }

static int valid_id(const char *s)
{
    if (!s || !*s || strlen(s) > 48) return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '-'))
            return 0;
    return 1;
}

static Json *ports_of(const char *node_id)
{
    Json *s = scene_by_id(node_id);
    if (s) return json_get(s, "ports");
    Json *t = json_find_id(json_get(ed.project, "tunnels"), node_id);
    return t ? json_get(t, "ports") : NULL;
}

static int port_exists(const char *ref)
{
    char node[64];
    const char *dot = strchr(ref, '.');
    if (!dot || dot - ref >= (int)sizeof(node)) return 0;
    memcpy(node, ref, (size_t)(dot - ref));
    node[dot - ref] = 0;
    Json *ports = ports_of(node);
    for (Json *p = ports ? ports->child : NULL; p; p = p->next)
        if (!strcmp(json_gets(p, "name", ""), dot + 1)) return 1;
    return 0;
}

// A free spot on the GRAPH page for a new node (no overlap with existing nodes).
static void free_graph_pos(float *out)
{
    Json *g = json_get(ed.project, "graph");
    for (int row = 0; row < 40; row++)
        for (int col = 0; col < 5; col++) {
            float x = 60.0f + col * 220.0f, y = 60.0f + row * 150.0f;
            int clash = 0;
            for (Json *e = g ? g->child : NULL; e && !clash; e = e->next) {
                float p[2] = {0, 0};
                int k = 0;
                for (Json *c = e->child; c && k < 2; c = c->next) p[k++] = (float)c->num;
                if (k == 2 && p[0] > x - 200 && p[0] < x + 200 && p[1] > y - 130 && p[1] < y + 130) clash = 1;
            }
            if (!clash) { out[0] = x; out[1] = y; return; }
        }
    out[0] = 60;
    out[1] = 60;
}

static Json *summary_scene(Json *s)
{
    Json *j = json_obj();
    json_sets(j, "id", scene_id(s));
    json_sets(j, "name", json_gets(s, "name", ""));
    json_sets(j, "kind", json_gets(s, "kind", "3d"));
    json_setb(j, "visible", json_getb(s, "visible", 1));
    json_setn(j, "objects", json_len(json_get(s, "objects")));
    int open = 0;
    Json *notes = json_get(s, "notes");
    for (Json *n = notes ? notes->child : NULL; n; n = n->next)
        if (strcmp(json_gets(n, "status", "open"), "done")) open++;
    json_setn(j, "notes_open", open);
    json_setn(j, "notes", json_len(notes));
    return j;
}

// ---------------------------------------------------------------- project
static Json *op_help(Json *a, const char *w, void *u) { (void)a; (void)w; (void)u; return ops_help(); }

static Json *op_project_get(Json *a, const char *w, void *u)
{
    (void)a; (void)w; (void)u;
    Json *r = ok();
    json_sets(r, "name", json_gets(ed.project, "name", ""));
    json_sets(r, "root", ed.root);
    json_sets(r, "current", scene_id(cur_scene()));
    Json *sc = json_set(r, "scenes", json_arr());
    for (int i = 0; i < ed.nscenes; i++) json_push(sc, summary_scene(ed.scenes[i]));
    json_set(r, "tunnels", json_clone(json_get(ed.project, "tunnels")));
    json_set(r, "lines", json_clone(json_get(ed.project, "lines")));
    json_sets(r, "files", "mexnya/project.json, mexnya/scenes/<id>.json, mexnya/NOTES.md");
    return r;
}

static Json *op_project_save(Json *a, const char *w, void *u)
{
    (void)a; (void)w; (void)u;
    return project_save() == 0 ? ok() : fail("save failed", NULL);
}

static Json *op_project_reload(Json *a, const char *w, void *u)
{
    (void)a; (void)w; (void)u;
    return project_reload() == 0 ? ok() : fail("reload failed (see the log)", NULL);
}

// ---------------------------------------------------------------- scenes
static Json *op_scene_create(Json *a, const char *w, void *u)
{
    (void)u;
    if (ed.nscenes >= ED_MAX_SCENES) return fail("too many scenes", NULL);
    const char *kind = json_gets(a, "kind", "3d");
    if (strcmp(kind, "3d") && strcmp(kind, "2d")) return fail("kind must be \"3d\" or \"2d\"", NULL);
    char id[64];
    const char *want = json_gets(a, "id", NULL);
    if (want) {
        if (!valid_id(want)) return fail("bad id \"%s\" (letters, digits, _ and -)", want);
        if (scene_index(want) >= 0) return fail("scene \"%s\" exists", want);
        snprintf(id, sizeof(id), "%s", want);
    } else make_id(id, sizeof(id), !strcmp(kind, "2d") ? "ui" : "scene", NULL);
    before_change("scene.create");
    Json *s = scene_new(id, json_gets(a, "name", id), kind);
    int at = (int)json_getn(a, "index", ed.nscenes);
    if (at < 0 || at > ed.nscenes) at = ed.nscenes;
    memmove(ed.scenes + at + 1, ed.scenes + at, sizeof(Json *) * (size_t)(ed.nscenes - at));
    memmove(ed.cams + at + 1, ed.cams + at, sizeof(Cam) * (size_t)(ed.nscenes - at));
    ed.scenes[at] = s;
    cam_default(&ed.cams[at]);
    ed.nscenes++;
    if (at <= ed.cur && !is_human(w)) ed.cur++;  // keep the person on their scene
    if (is_human(w)) ed.cur = at;
    Json *gpos = json_get(ed.project, "graph");
    if (!gpos) gpos = json_set(ed.project, "graph", json_obj());
    float p[2];
    free_graph_pos(p);
    json_setv(gpos, id, p, 2);
    Json *r = ok();
    json_sets(r, "id", id);
    return r;
}

static Json *op_scene_delete(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    int i;
    Json *s = arg_scene(a, &i);
    if (!s) return fail("no such scene", NULL);
    before_change("scene.delete");
    char id[64];
    snprintf(id, sizeof(id), "%s", scene_id(s));
    // Lines touching the scene go with it.
    Json *lines = json_get(ed.project, "lines");
    size_t n = strlen(id);
    for (Json *l = lines ? lines->child : NULL; l;) {
        Json *next = l->next;
        const char *f = json_gets(l, "from", ""), *t = json_gets(l, "to", "");
        if ((!strncmp(f, id, n) && f[n] == '.') || (!strncmp(t, id, n) && t[n] == '.')) json_free(json_detach(l));
        l = next;
    }
    json_remove(json_get(ed.project, "graph"), id);
    json_free(s);
    memmove(ed.scenes + i, ed.scenes + i + 1, sizeof(Json *) * (size_t)(ed.nscenes - i - 1));
    memmove(ed.cams + i, ed.cams + i + 1, sizeof(Cam) * (size_t)(ed.nscenes - i - 1));
    ed.nscenes--;
    if (ed.cur >= ed.nscenes) ed.cur = ed.nscenes - 1;
    char path[ED_PATH], rel[200];
    snprintf(rel, sizeof(rel), "scenes/%s.json", id);
    ed_path(path, sizeof(path), rel);
    plat_remove_file(path);
    return ok();
}

static Json *op_scene_get(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    int i;
    Json *s = arg_scene(a, &i);
    if (!s) return fail("no such scene", NULL);
    Json *r = ok();
    Json *c = json_set(r, "scene", json_clone(s));
    if (!scene_is_2d(s)) json_set(c, "camera", cam_to_json(&ed.cams[i]));
    return r;
}

static Json *op_scene_set(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *s = arg_scene(a, NULL);
    if (!s) return fail("no such scene", NULL);
    Json *f = fields_of(a);
    json_remove(f, "objects");
    json_remove(f, "notes");
    json_remove(f, "kind");
    before_change("scene.set");
    // settings merge one level deep, so {"settings": {"fog": [..]}} keeps the rest.
    Json *set = json_get(f, "settings");
    if (set && set->type == JSON_OBJ) {
        Json *dst = json_get(s, "settings");
        if (!dst) dst = json_set(s, "settings", json_obj());
        merge(dst, set, 0);
        json_remove(f, "settings");
    }
    merge(s, f, 1);
    json_free(f);
    return ok();
}

static Json *op_scene_move(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    int i;
    Json *s = arg_scene(a, &i);
    if (!s) return fail("no such scene", NULL);
    int to = (int)json_getn(a, "index", i);
    if (to < 0) to = 0;
    if (to >= ed.nscenes) to = ed.nscenes - 1;
    if (to == i) return ok();
    before_change("scene.move");
    Cam c = ed.cams[i];
    if (to < i) {
        memmove(ed.scenes + to + 1, ed.scenes + to, sizeof(Json *) * (size_t)(i - to));
        memmove(ed.cams + to + 1, ed.cams + to, sizeof(Cam) * (size_t)(i - to));
    } else {
        memmove(ed.scenes + i, ed.scenes + i + 1, sizeof(Json *) * (size_t)(to - i));
        memmove(ed.cams + i, ed.cams + i + 1, sizeof(Cam) * (size_t)(to - i));
    }
    ed.scenes[to] = s;
    ed.cams[to] = c;
    ed.cur = to;
    return ok();
}

static Json *op_scene_select(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    int i;
    if (!arg_scene(a, &i)) return fail("no such scene", NULL);
    ed.cur = i;
    ed.sel[0] = ed.sel_note[0] = 0;
    return ok();
}

// ---------------------------------------------------------------- objects
static void object_defaults(Json *o, const char *type, int is2d)
{
    if (is2d) {
        float r[4] = {20, 20, 80, 24};
        if (!strcmp(type, "text")) { r[2] = 120; r[3] = 16; }
        if (!json_get(o, "rect")) json_setv(o, "rect", r, 4);
        if (!json_get(o, "color")) json_sets(o, "color", !strcmp(type, "text") ? "#E6E6E6" : "#3A3D42");
        if ((!strcmp(type, "text") || !strcmp(type, "button")) && !json_get(o, "text")) json_sets(o, "text", "Text");
        return;
    }
    float p[3] = {0, 0.5f, 0}, r[3] = {0, 0, 0}, s[3] = {1, 1, 1};
    if (!strcmp(type, "plane")) { p[1] = 0; s[0] = s[2] = 4; }
    if (!strcmp(type, "light")) p[1] = 3;
    if (!json_get(o, "pos")) json_setv(o, "pos", p, 3);
    if (!json_get(o, "rot")) json_setv(o, "rot", r, 3);
    if (!json_get(o, "scale")) json_setv(o, "scale", s, 3);
    if (!json_get(o, "color")) json_sets(o, "color", !strcmp(type, "light") ? "#FFF2D8" : "#B4B8BE");
}

static const char *const TYPES_3D[] = {"box", "plane", "sphere", "cylinder", "cone", "model", "light", "marker", "camera", NULL};
static const char *const TYPES_2D[] = {"rect", "text", "button", "image", "panel", NULL};

static int type_ok(const char *t, int is2d)
{
    for (const char *const *p = is2d ? TYPES_2D : TYPES_3D; *p; p++)
        if (!strcmp(*p, t)) return 1;
    return 0;
}

static Json *op_object_add(Json *a, const char *w, void *u)
{
    (void)u;
    Json *s = arg_scene(a, NULL);
    if (!s) return fail("no such scene", NULL);
    int is2d = scene_is_2d(s);
    const char *type = json_gets(a, "type", is2d ? "rect" : "box");
    if (!type_ok(type, is2d))
        return fail(is2d ? "2D types: rect text button image panel (got %s)"
                         : "3D types: box plane sphere cylinder cone model light marker camera (got %s)", type);
    Json *objs = scene_objects(s);
    char id[64];
    const char *want = json_gets(a, "id", NULL);
    if (want) {
        if (!valid_id(want)) return fail("bad id \"%s\"", want);
        if (json_find_id(objs, want)) return fail("object \"%s\" exists", want);
        snprintf(id, sizeof(id), "%s", want);
    } else make_id(id, sizeof(id), type, objs);
    before_change("object.add");
    Json *o = json_obj();
    json_sets(o, "id", id);
    json_sets(o, "name", json_gets(a, "name", id));
    json_sets(o, "type", type);
    Json *f = fields_of(a);
    json_remove(f, "type");
    json_remove(f, "index");
    merge(o, f, 1);
    json_free(f);
    object_defaults(o, type, is2d);
    json_push(objs, o);
    if (is_human(w)) snprintf(ed.sel, sizeof(ed.sel), "%s", id);  // agents don't steal the selection
    Json *r = ok();
    json_sets(r, "id", id);
    return r;
}

static Json *op_object_set(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *s = arg_scene(a, NULL);
    Json *o = find_object(s, json_gets(a, "id", NULL));
    if (!o) return fail("no such object \"%s\"", json_gets(a, "id", "?"));
    Json *f = fields_of(a);
    if (json_get(f, "type") && !type_ok(json_gets(f, "type", ""), scene_is_2d(s))) { json_free(f); return fail("bad type", NULL); }
    before_change("object.set");
    merge(o, f, 1);
    json_free(f);
    return ok();
}

static Json *op_object_delete(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *s = arg_scene(a, NULL);
    const char *id = json_gets(a, "id", NULL);
    Json *o = find_object(s, id);
    if (!o) return fail("no such object \"%s\"", id);
    before_change("object.delete");
    if (!strcmp(ed.sel, id)) ed.sel[0] = 0;
    json_free(json_detach(o));
    return ok();
}

static Json *op_object_get(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *o = find_object(arg_scene(a, NULL), json_gets(a, "id", NULL));
    if (!o) return fail("no such object \"%s\"", json_gets(a, "id", "?"));
    Json *r = ok();
    json_set(r, "object", json_clone(o));
    return r;
}

static Json *op_object_list(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *s = arg_scene(a, NULL);
    if (!s) return fail("no such scene", NULL);
    Json *r = ok();
    json_sets(r, "scene", scene_id(s));
    json_set(r, "objects", json_clone(scene_objects(s)));
    return r;
}

static Json *op_object_duplicate(Json *a, const char *w, void *u)
{
    (void)u;
    Json *s = arg_scene(a, NULL);
    Json *o = find_object(s, json_gets(a, "id", NULL));
    if (!o) return fail("no such object \"%s\"", json_gets(a, "id", "?"));
    before_change("object.duplicate");
    Json *c = json_clone(o);
    char id[64];
    make_id(id, sizeof(id), json_gets(o, "type", "obj"), scene_objects(s));
    json_sets(c, "id", id);
    float p[4];
    if (json_getv(c, "pos", p, 3) == 3) { p[0] += 1; json_setv(c, "pos", p, 3); }
    if (json_getv(c, "rect", p, 4) == 4) { p[0] += 8; p[1] += 8; json_setv(c, "rect", p, 4); }
    free(c->key);
    c->key = NULL;
    json_push(scene_objects(s), c);
    if (is_human(w)) snprintf(ed.sel, sizeof(ed.sel), "%s", id);
    Json *r = ok();
    json_sets(r, "id", id);
    return r;
}

// ---------------------------------------------------------------- notes
static Json *op_note_add(Json *a, const char *w, void *u)
{
    (void)u;
    int si;
    Json *s = arg_scene(a, &si);
    if (!s) return fail("no such scene", NULL);
    const char *kind = json_gets(a, "kind", "box");
    if (strcmp(kind, "box") && strcmp(kind, "circle") && strcmp(kind, "arrow") && strcmp(kind, "pin") && strcmp(kind, "pen"))
        return fail("kind: box circle arrow pin pen (got %s)", kind);
    Json *shape = json_get(a, "shape");
    if (!shape || shape->type != JSON_OBJ) return fail("shape is required, e.g. {\"rect\": [x, y, w, h]}", NULL);
    Json *notes = scene_notes(s);
    char id[64];
    make_id(id, sizeof(id), "note", notes);
    before_change("note.add");
    Json *n = json_obj();
    json_sets(n, "id", id);
    json_sets(n, "kind", kind);
    json_sets(n, "text", json_gets(a, "text", ""));
    json_sets(n, "status", "open");
    json_sets(n, "author", w ? w : "?");
    char t[32];
    ed_now_iso(t, sizeof(t));
    json_sets(n, "created", t);
    json_sets(n, "color", json_gets(a, "color", strcmp(w ? w : "", "human") ? "#B18FE0" : "#E8B04A"));
    json_set(n, "shape", json_clone(shape));
    Cam cam = ed.cams[si];
    Json *cj = json_get(a, "camera");
    if (cj) cam_from_json(&cam, cj);
    int vw = (int)json_getn(a, "view_w", ed.view.w > 0 ? ed.view.w : 542);
    int vh = (int)json_getn(a, "view_h", ed.view.h > 0 ? ed.view.h : 388);
    if (!scene_is_2d(s)) {
        json_set(n, "camera", cam_to_json(&cam));
        float vs[2] = {(float)vw, (float)vh};
        json_setv(n, "view_size", vs, 2);
    }
    // Targets and world anchor: computed from the shape unless the caller gave them.
    View v;
    if (scene_is_2d(s)) {
        float c[2] = {1280, 720};
        json_getv(json_get(s, "settings"), "canvas", c, 2);
        memset(&v, 0, sizeof(v));
        v.w = (int)c[0]; v.h = (int)c[1]; v.cscale = 1; v.canvas_w = v.w; v.canvas_h = v.h;
    } else view3d_setup(&v, &cam, 0, 0, vw, vh);
    float b[4] = {0, 0, 0, 0}, cx = 0, cy = 0;
    float rr[4];
    if (json_getv(shape, "rect", rr, 4) == 4) { b[0] = rr[0]; b[1] = rr[1]; b[2] = rr[0] + rr[2]; b[3] = rr[1] + rr[3]; }
    else if (json_getv(shape, "circle", rr, 3) == 3) { b[0] = rr[0] - rr[2]; b[1] = rr[1] - rr[2]; b[2] = rr[0] + rr[2]; b[3] = rr[1] + rr[2]; }
    else if (json_getv(shape, "arrow", rr, 4) == 4) { b[0] = rr[2] - 6; b[1] = rr[3] - 6; b[2] = rr[2] + 6; b[3] = rr[3] + 6; }
    else if (json_getv(shape, "pin", rr, 2) == 2) { b[0] = rr[0] - 4; b[1] = rr[1] - 4; b[2] = rr[0] + 4; b[3] = rr[1] + 4; }
    else {
        Json *pts = json_get(shape, "points");
        b[0] = b[1] = 1e9f; b[2] = b[3] = -1e9f;
        for (Json *p = pts ? pts->child : NULL; p; p = p->next) {
            float q[2];
            int k = 0;
            for (Json *e = p->child; e && k < 2; e = e->next) q[k++] = (float)e->num;
            if (k < 2) continue;
            if (q[0] < b[0]) b[0] = q[0];
            if (q[1] < b[1]) b[1] = q[1];
            if (q[0] > b[2]) b[2] = q[0];
            if (q[1] > b[3]) b[3] = q[1];
        }
        if (b[0] > b[2]) { b[0] = b[1] = b[2] = b[3] = 0; }
    }
    cx = (b[0] + b[2]) * 0.5f;
    cy = (b[1] + b[3]) * 0.5f;
    if (!json_get(a, "targets")) {
        Json *tg = json_arr();
        // Shapes are in view (or canvas) pixels; the view helpers expect screen pixels.
        objects_in_rect(&v, s, b[0] + v.x, b[1] + v.y, b[2] + v.x, b[3] + v.y, tg);
        json_set(n, "targets", tg);
    } else json_set(n, "targets", json_clone(json_get(a, "targets")));
    if (!scene_is_2d(s)) {
        float hit[3];
        char pid[64];
        Json *wv = json_get(a, "world");
        if (wv) json_set(n, "world", json_clone(wv));
        else if (pick_object(&v, s, cx, cy, pid, sizeof(pid), hit)) json_setv(n, "world", hit, 3);
    }
    json_push(notes, n);
    if (is_human(w)) snprintf(ed.sel_note, sizeof(ed.sel_note), "%s", id);
    if (json_getb(a, "snapshot", 1)) {
        char rel[200], path[ED_PATH];
        snprintf(rel, sizeof(rel), "notes/%s/%s.png", scene_id(s), id);
        ed_path(path, sizeof(path), rel);
        // In the GUI the capture happens at the start of the next frame; the path is final.
        if (render_snapshot(s, &cam, vw, vh, id, path) == 0) json_sets(n, "image", rel);
    }
    Json *r = ok();
    json_sets(r, "id", id);
    json_set(r, "note", json_clone(n));
    return r;
}

static Json *op_note_set(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *n = find_note(arg_scene(a, NULL), json_gets(a, "id", NULL));
    if (!n) return fail("no such note \"%s\"", json_gets(a, "id", "?"));
    Json *f = fields_of(a);
    before_change("note.set");
    merge(n, f, 1);
    json_free(f);
    return ok();
}

static Json *add_reply(Json *n, const char *text, const char *w)
{
    Json *rep = json_get(n, "replies");
    if (!rep || rep->type != JSON_ARR) rep = json_set(n, "replies", json_arr());
    Json *r = json_obj();
    json_sets(r, "author", w ? w : "?");
    char t[32];
    ed_now_iso(t, sizeof(t));
    json_sets(r, "t", t);
    json_sets(r, "text", text);
    return json_push(rep, r);
}

static Json *op_note_reply(Json *a, const char *w, void *u)
{
    (void)u;
    Json *n = find_note(arg_scene(a, NULL), json_gets(a, "id", NULL));
    if (!n) return fail("no such note \"%s\"", json_gets(a, "id", "?"));
    before_change("note.reply");
    add_reply(n, json_gets(a, "text", ""), w);
    return ok();
}

static Json *op_note_resolve(Json *a, const char *w, void *u)
{
    (void)u;
    Json *n = find_note(arg_scene(a, NULL), json_gets(a, "id", NULL));
    if (!n) return fail("no such note \"%s\"", json_gets(a, "id", "?"));
    before_change("note.resolve");
    json_sets(n, "status", json_getb(a, "reopen", 0) ? "open" : "done");
    const char *text = json_gets(a, "reply", NULL);
    if (text) add_reply(n, text, w);
    return ok();
}

static Json *op_note_delete(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *s = arg_scene(a, NULL);
    Json *n = find_note(s, json_gets(a, "id", NULL));
    if (!n) return fail("no such note \"%s\"", json_gets(a, "id", "?"));
    before_change("note.delete");
    char path[ED_PATH];
    const char *img = json_gets(n, "image", NULL);
    if (img) { ed_path(path, sizeof(path), img); plat_remove_file(path); }
    json_free(json_detach(n));
    return ok();
}

static Json *op_note_list(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *only = json_gets(a, "scene", NULL);
    const char *status = json_gets(a, "status", NULL);
    Json *r = ok();
    Json *list = json_set(r, "notes", json_arr());
    for (int i = 0; i < ed.nscenes; i++) {
        Json *s = ed.scenes[i];
        if (only && strcmp(only, scene_id(s))) continue;
        Json *notes = json_get(s, "notes");
        for (Json *n = notes ? notes->child : NULL; n; n = n->next) {
            if (status && strcmp(status, json_gets(n, "status", "open"))) continue;
            Json *c = json_clone(n);
            json_sets(c, "scene", scene_id(s));
            free(c->key);
            c->key = NULL;
            json_push(list, c);
        }
    }
    json_sets(r, "markdown", "mexnya/NOTES.md");
    return r;
}

// ---------------------------------------------------------------- tunnels, ports, lines
static Json *op_tunnel_add(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *tunnels = json_get(ed.project, "tunnels");
    if (!tunnels) tunnels = json_set(ed.project, "tunnels", json_arr());
    char id[64];
    const char *want = json_gets(a, "id", NULL);
    if (want) {
        if (!valid_id(want)) return fail("bad id \"%s\"", want);
        if (json_find_id(tunnels, want) || scene_index(want) >= 0) return fail("\"%s\" exists", want);
        snprintf(id, sizeof(id), "%s", want);
    } else make_id(id, sizeof(id), "tunnel", NULL);
    before_change("tunnel.add");
    Json *t = json_obj();
    json_sets(t, "id", id);
    json_sets(t, "name", json_gets(a, "name", id));
    json_sets(t, "note", json_gets(a, "note", ""));
    Json *ports = json_set(t, "ports", json_arr());
    Json *pa = json_get(a, "ports");
    for (Json *p = pa ? pa->child : NULL; p; p = p->next) {
        Json *q = json_obj();
        if (p->type == JSON_STR) { json_sets(q, "name", p->str); json_sets(q, "dir", "both"); }
        else { json_sets(q, "name", json_gets(p, "name", "port")); json_sets(q, "dir", json_gets(p, "dir", "both")); }
        json_push(ports, q);
    }
    json_push(tunnels, t);
    Json *g = json_get(ed.project, "graph");
    if (!g) g = json_set(ed.project, "graph", json_obj());
    float pos[2];
    free_graph_pos(pos);
    if (json_get(a, "x")) { pos[0] = (float)json_getn(a, "x", pos[0]); pos[1] = (float)json_getn(a, "y", pos[1]); }
    json_setv(g, id, pos, 2);
    Json *r = ok();
    json_sets(r, "id", id);
    return r;
}

static void drop_lines_of(const char *node, const char *port)
{
    char ref[140];
    size_t n;
    if (port) snprintf(ref, sizeof(ref), "%s.%s", node, port);
    else snprintf(ref, sizeof(ref), "%s.", node);
    n = strlen(ref);
    Json *lines = json_get(ed.project, "lines");
    for (Json *l = lines ? lines->child : NULL; l;) {
        Json *next = l->next;
        const char *f = json_gets(l, "from", ""), *t = json_gets(l, "to", "");
        int hit = port ? (!strcmp(f, ref) || !strcmp(t, ref)) : (!strncmp(f, ref, n) || !strncmp(t, ref, n));
        if (hit) json_free(json_detach(l));
        l = next;
    }
}

static Json *op_tunnel_delete(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *id = json_gets(a, "id", NULL);
    Json *t = json_find_id(json_get(ed.project, "tunnels"), id);
    if (!t) return fail("no such tunnel \"%s\"", id);
    before_change("tunnel.delete");
    drop_lines_of(id, NULL);
    json_remove(json_get(ed.project, "graph"), id);
    json_free(json_detach(t));
    return ok();
}

static Json *op_tunnel_set(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *t = json_find_id(json_get(ed.project, "tunnels"), json_gets(a, "id", NULL));
    if (!t) return fail("no such tunnel \"%s\"", json_gets(a, "id", "?"));
    Json *f = fields_of(a);
    before_change("tunnel.set");
    merge(t, f, 1);
    json_free(f);
    return ok();
}

static Json *op_port_add(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *node = json_gets(a, "node", NULL), *name = json_gets(a, "name", NULL);
    if (!node || !valid_id(name)) return fail("port.add needs node and a port name (letters, digits, _)", NULL);
    Json *s = scene_by_id(node);
    Json *t = s ? NULL : json_find_id(json_get(ed.project, "tunnels"), node);
    if (!s && !t) return fail("no scene or tunnel \"%s\"", node);
    Json *holder = s ? s : t;
    Json *ports = json_get(holder, "ports");
    if (!ports) ports = json_set(holder, "ports", json_arr());
    for (Json *p = ports->child; p; p = p->next)
        if (!strcmp(json_gets(p, "name", ""), name)) return fail("port \"%s\" exists", name);
    before_change("port.add");
    Json *p = json_obj();
    json_sets(p, "name", name);
    json_sets(p, "dir", json_gets(a, "dir", s ? "out" : "both"));
    json_push(ports, p);
    return ok();
}

static Json *op_port_delete(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *node = json_gets(a, "node", NULL), *name = json_gets(a, "name", NULL);
    Json *ports = node ? ports_of(node) : NULL;
    for (Json *p = ports ? ports->child : NULL; p; p = p->next)
        if (!strcmp(json_gets(p, "name", ""), name ? name : "")) {
            before_change("port.delete");
            drop_lines_of(node, name);
            json_free(json_detach(p));
            return ok();
        }
    return fail("no such port", NULL);
}

static Json *op_line_add(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *from = json_gets(a, "from", NULL), *to = json_gets(a, "to", NULL);
    if (!from || !to) return fail("line.add needs from and to, e.g. \"world.exit\" -> \"ui_bus.open_menu\"", NULL);
    if (!port_exists(from)) return fail("no such port \"%s\" (node.port)", from);
    if (!port_exists(to)) return fail("no such port \"%s\" (node.port)", to);
    if (!strcmp(from, to)) return fail("a line needs two different ports", NULL);
    Json *lines = json_get(ed.project, "lines");
    if (!lines) lines = json_set(ed.project, "lines", json_arr());
    for (Json *l = lines->child; l; l = l->next)
        if (!strcmp(json_gets(l, "from", ""), from) && !strcmp(json_gets(l, "to", ""), to))
            return fail("that line exists (%s)", json_gets(l, "id", ""));
    char id[64];
    make_id(id, sizeof(id), "line", lines);
    before_change("line.add");
    Json *l = json_obj();
    json_sets(l, "id", id);
    json_sets(l, "from", from);
    json_sets(l, "to", to);
    json_sets(l, "note", json_gets(a, "note", ""));
    json_push(lines, l);
    Json *r = ok();
    json_sets(r, "id", id);
    return r;
}

static Json *op_line_delete(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *l = json_find_id(json_get(ed.project, "lines"), json_gets(a, "id", NULL));
    if (!l) return fail("no such line \"%s\"", json_gets(a, "id", "?"));
    before_change("line.delete");
    json_free(json_detach(l));
    return ok();
}

static Json *op_line_set(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    Json *l = json_find_id(json_get(ed.project, "lines"), json_gets(a, "id", NULL));
    if (!l) return fail("no such line \"%s\"", json_gets(a, "id", "?"));
    Json *f = fields_of(a);
    const char *nf = json_gets(f, "from", NULL), *nt = json_gets(f, "to", NULL);
    if ((nf && !port_exists(nf)) || (nt && !port_exists(nt))) { json_free(f); return fail("no such port", NULL); }
    before_change("line.set");
    merge(l, f, 1);
    json_free(f);
    return ok();
}

static Json *op_graph_layout(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *id = json_gets(a, "id", NULL);
    float p[2];
    if (!id || (scene_index(id) < 0 && !json_find_id(json_get(ed.project, "tunnels"), id)))
        return fail("no scene or tunnel \"%s\"", id);
    if (json_getv(a, "pos", p, 2) != 2) return fail("pos: [x, y]", NULL);
    before_change("graph.layout");
    Json *g = json_get(ed.project, "graph");
    if (!g) g = json_set(ed.project, "graph", json_obj());
    json_setv(g, id, p, 2);
    return ok();
}

// Follows lines from a port: what would happen at runtime. Tunnels forward what
// arrives on a port to every line leaving the same port.
static Json *op_graph_emit(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *from = json_gets(a, "from", NULL);
    if (!from || !port_exists(from)) return fail("no such port \"%s\"", from);
    Json *r = ok();
    Json *steps = json_set(r, "reached", json_arr());
    char queue[64][140];
    int qh = 0, qt = 0;
    snprintf(queue[qt++], 140, "%s", from);
    Json *lines = json_get(ed.project, "lines");
    Json *path = json_set(r, "lines", json_arr());
    while (qh < qt && json_len(steps) < 64) {
        const char *cur = queue[qh++];
        for (Json *l = lines ? lines->child : NULL; l; l = l->next) {
            if (strcmp(json_gets(l, "from", ""), cur)) continue;
            const char *to = json_gets(l, "to", "");
            int seen = 0;
            for (Json *s = steps->child; s; s = s->next) if (!strcmp(s->str, to)) seen = 1;
            if (seen) continue;
            json_push(steps, json_str(to));
            json_push(path, json_str(json_gets(l, "id", "")));
            char node[64];
            const char *dot = strchr(to, '.');
            int n = dot ? (int)(dot - to) : 0;
            if (n <= 0 || n >= 64) continue;
            memcpy(node, to, (size_t)n);
            node[n] = 0;
            if (json_find_id(json_get(ed.project, "tunnels"), node) && qt < 64)
                snprintf(queue[qt++], 140, "%s", to);
        }
    }
    return r;
}

// ---------------------------------------------------------------- view
static int preset(Cam *c, const char *p)
{
    if (!p) return 0;
    if (!strcmp(p, "top")) { c->yaw = 0; c->pitch = 89.9f; c->ortho = 1; }
    else if (!strcmp(p, "front")) { c->yaw = 0; c->pitch = 0; c->ortho = 1; }
    else if (!strcmp(p, "right")) { c->yaw = 90; c->pitch = 0; c->ortho = 1; }
    else if (!strcmp(p, "back")) { c->yaw = 180; c->pitch = 0; c->ortho = 1; }
    else if (!strcmp(p, "left")) { c->yaw = -90; c->pitch = 0; c->ortho = 1; }
    else if (!strcmp(p, "persp")) { c->ortho = 0; if (c->pitch > 80) c->pitch = 30; }
    else return 0;
    return 1;
}

static Json *op_view_set(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    int i;
    Json *s = arg_scene(a, &i);
    if (!s) return fail("no such scene", NULL);
    Json *c = json_get(a, "camera");
    if (c) {
        Cam cam = ed.cams[i];
        json_getv(c, "target", cam.target, 3);
        cam.yaw = (float)json_getn(c, "yaw", cam.yaw);
        cam.pitch = (float)json_getn(c, "pitch", cam.pitch);
        cam.dist = (float)json_getn(c, "dist", cam.dist);
        cam.fov = (float)json_getn(c, "fov", cam.fov);
        cam.ortho = json_getb(c, "ortho", cam.ortho);
        ed.cams[i] = cam;
    }
    const char *p = json_gets(a, "preset", NULL);
    if (p && !preset(&ed.cams[i], p)) return fail("preset: top front right back left persp (got %s)", p);
    ed.cur = i;
    ed.dirty = 1;
    Json *r = ok();
    json_set(r, "camera", cam_to_json(&ed.cams[i]));
    return r;
}

static Json *op_view_get(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    int i;
    if (!arg_scene(a, &i)) return fail("no such scene", NULL);
    Json *r = ok();
    json_set(r, "camera", cam_to_json(&ed.cams[i]));
    float vs[2] = {(float)ed.view.w, (float)ed.view.h};
    json_setv(r, "view_size", vs, 2);
    return r;
}

static Json *op_view_snapshot(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    int i;
    Json *s = arg_scene(a, &i);
    if (!s) return fail("no such scene", NULL);
    Cam cam = ed.cams[i];
    Json *cj = json_get(a, "camera");
    if (cj) cam_from_json(&cam, cj);
    preset(&cam, json_gets(a, "preset", NULL));
    int vw = (int)json_getn(a, "width", ed.view.w > 0 ? ed.view.w : 542);
    int vh = (int)json_getn(a, "height", ed.view.h > 0 ? ed.view.h : 388);
    if (vw < 16) vw = 16;
    if (vh < 16) vh = 16;
    if (vw > 2048) vw = 2048;
    if (vh > 2048) vh = 2048;
    char path[ED_PATH], rel[200];
    const char *want = json_gets(a, "path", NULL);
    if (want) snprintf(rel, sizeof(rel), "%s", want);
    else snprintf(rel, sizeof(rel), "snapshots/%s.png", scene_id(s));
    if (rel[0] == '/' || strstr(rel, "..")) return fail("path must stay inside mexnya/ (got %s)", rel);
    ed_path(path, sizeof(path), rel);
    if (render_snapshot(s, &cam, vw, vh, json_getb(a, "notes", 1) ? "" : NULL, path) != 0)
        return fail("snapshot failed", NULL);
    Json *r = ok();
    json_sets(r, "path", path);
    json_sets(r, "relative", rel);
    if (!scene_is_2d(s)) json_set(r, "camera", cam_to_json(&cam));
    json_sets(r, "pixels", scene_is_2d(s) ? "1 image pixel = 1 canvas pixel" : "image = the viewport at this camera");
    return r;
}

static Json *op_select(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    int i;
    Json *s = arg_scene(a, &i);
    if (!s) return fail("no such scene", NULL);
    ed.cur = i;
    const char *id = json_gets(a, "id", "");
    if (id[0] && !find_object(s, id)) return fail("no such object \"%s\"", id);
    snprintf(ed.sel, sizeof(ed.sel), "%s", id);
    return ok();
}

// ---------------------------------------------------------------- tools
static Json *op_script_run(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *code = json_gets(a, "code", NULL);
    const char *file = json_gets(a, "file", NULL);
    if (file) {
        char path[ED_PATH];
        if (file[0] == '/' || strstr(file, "..")) return fail("file must be inside mexnya/ (got %s)", file);
        ed_path(path, sizeof(path), file);
        int size = 0;
        char *buf = (char *)plat_read_file(path, &size, 0);
        if (!buf) return fail("cannot read %s", path);
        char *t = (char *)malloc((size_t)size + 1);
        memcpy(t, buf, (size_t)size);
        t[size] = 0;
        free(buf);
        Json *r = script_run(t, file);
        free(t);
        return r;
    }
    if (!code) return fail("script.run needs code or file", NULL);
    return script_run(code, "agent");
}

static Json *run_tool(const char *cmd)
{
    Json *c = run_capture(cmd, 1 << 18);
    Json *r = ok();
    json_setn(r, "code", json_getn(c, "code", -1));
    json_sets(r, "output", json_gets(c, "output", ""));
    if (json_getn(c, "code", -1) != 0) json_setb(r, "ok", 0), json_sets(r, "error", "command failed");
    json_free(c);
    return r;
}

static void shell_quote(char *out, int size, const char *s)
{
    int n = 0;
#ifdef _WIN32
    out[n++] = '"';
    for (; *s && n < size - 3; s++) { if (*s == '"') out[n++] = '\\'; out[n++] = *s; }
    out[n++] = '"';
#else
    out[n++] = '\'';
    for (; *s && n < size - 6; s++) {
        if (*s == '\'') { memcpy(out + n, "'\\''", 4); n += 4; }
        else out[n++] = *s;
    }
    out[n++] = '\'';
#endif
    out[n] = 0;
}

static Json *op_build_run(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *target = json_gets(a, "target", "debug");
    for (const char *p = target; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || *p == '-' || (*p >= '0' && *p <= '9'))) return fail("bad target %s", target);
    if (job_running()) return fail("a build is already running", NULL);
    char cmd[ED_PATH * 2], root[ED_PATH + 8];
    shell_quote(root, sizeof(root), ed.root);
    const char *verb = !strcmp(target, "test") ? "test" : (!strncmp(target, "package", 7) ? "package" : "build");
    const char *arg = target;
    if (!strncmp(target, "package-", 8)) arg = target + 8;
    else if (!strcmp(target, "package") || !strcmp(target, "test")) arg = "";
    char tool[ED_PATH + 80];
    shell_quote(tool, sizeof(tool), ed_tool_path());
    snprintf(cmd, sizeof(cmd), "cd %s && python3 %s %s %s 2>&1", root, tool, verb, arg);
    if (job_start(target, cmd, CH_BUILD) != 0) return fail("could not start the build", NULL);
    Json *r = ok();
    json_sets(r, "started", cmd);
    json_sets(r, "follow", "build.status");
    return r;
}

static Json *op_build_status(Json *a, const char *w, void *u)
{
    (void)a; (void)w; (void)u;
    Json *r = ok();
    json_setb(r, "running", job_running());
    Json *lines = json_set(r, "tail", json_arr());
    int n = log_count(CH_BUILD), from = n > 40 ? n - 40 : 0;
    for (int i = from; i < n; i++) json_push(lines, json_str(log_line(CH_BUILD, i, NULL)));
    return r;
}

static Json *git(const char *args)
{
    char cmd[ED_PATH * 3], root[ED_PATH + 8];
    shell_quote(root, sizeof(root), ed.root);
    snprintf(cmd, sizeof(cmd), "git -C %s %s 2>&1", root, args);
    return run_tool(cmd);
}

static Json *op_git_status(Json *a, const char *w, void *u)
{
    (void)a; (void)w; (void)u;
    return git("status --porcelain=v1 --branch");
}

static Json *op_git_log(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    char args[64];
    int n = (int)json_getn(a, "n", 20);
    if (n < 1 || n > 500) n = 20;
    snprintf(args, sizeof(args), "log --oneline --decorate -n %d", n);
    return git(args);
}

static Json *op_git_diff(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *p = json_gets(a, "path", NULL);
    char args[ED_PATH + 64], q[ED_PATH];
    if (p) { shell_quote(q, sizeof(q), p); snprintf(args, sizeof(args), "diff --stat --patch -- %s", q); }
    else snprintf(args, sizeof(args), "diff --stat");
    return git(args);
}

static Json *op_git_commit(Json *a, const char *w, void *u)
{
    (void)u;
    const char *msg = json_gets(a, "message", NULL);
    if (!msg || !*msg) return fail("git.commit needs a message", NULL);
    project_save();
    // The message goes through a file: no shell quoting of user text.
    char path[ED_PATH], q[ED_PATH + 8], args[ED_PATH + 64];
    ed_path(path, sizeof(path), "agent/commit-msg.txt");
    char full[2048];
    snprintf(full, sizeof(full), "%s\n", msg);
    if (w && strcmp(w, "human")) snprintf(full + strlen(full), sizeof(full) - strlen(full), "\nOperation by agent: %s\n", w);
    plat_write_file(path, full, (int)strlen(full));
    if (json_getb(a, "all", 1)) {
        Json *add = git("add -A");
        int good = json_getb(add, "ok", 0);
        if (!good) return add;
        json_free(add);
    }
    shell_quote(q, sizeof(q), path);
    snprintf(args, sizeof(args), "commit -F %s", q);
    Json *r = git(args);
    plat_remove_file(path);
    return r;
}

static Json *op_git_simple(const char *what)
{
    return git(what);
}
static Json *op_git_push(Json *a, const char *w, void *u) { (void)a; (void)w; (void)u; return op_git_simple("push"); }
static Json *op_git_pull(Json *a, const char *w, void *u) { (void)a; (void)w; (void)u; return op_git_simple("pull --ff-only"); }
static Json *op_git_fetch(Json *a, const char *w, void *u) { (void)a; (void)w; (void)u; return op_git_simple("fetch --all --prune"); }
static Json *op_git_init(Json *a, const char *w, void *u) { (void)a; (void)w; (void)u; return op_git_simple("init"); }
static Json *op_git_branch(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    const char *name = json_gets(a, "switch", NULL);
    if (!name) return git("branch --all --verbose");
    if (!valid_id(name) && !strchr(name, '/')) return fail("bad branch name %s", name);
    char args[200];
    snprintf(args, sizeof(args), "switch %s", name);
    return git(args);
}

static Json *op_plugin_list(Json *a, const char *w, void *u)
{
    (void)a; (void)w; (void)u;
    Json *r = ok();
    json_set(r, "plugins", plugins_list());
    return r;
}

static Json *op_plugin_reload(Json *a, const char *w, void *u)
{
    (void)a; (void)w; (void)u;
    plugins_load();
    return ok();
}

static Json *op_log_tail(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    int n = (int)json_getn(a, "n", 30);
    int ch = CH_AGENT;
    const char *which = json_gets(a, "channel", "agent");
    if (!strcmp(which, "log")) ch = CH_LOG;
    else if (!strcmp(which, "build")) ch = CH_BUILD;
    else if (!strcmp(which, "lua")) ch = CH_LUA;
    Json *r = ok();
    Json *lines = json_set(r, "lines", json_arr());
    int c = log_count(ch);
    for (int i = c > n ? c - n : 0; i < c; i++) json_push(lines, json_str(log_line(ch, i, NULL)));
    json_sets(r, "file", "mexnya/agent/log.jsonl");
    return r;
}

static Json *op_undo(Json *a, const char *w, void *u) { (void)a; (void)w; (void)u; if (!ops_can_undo()) return fail("nothing to undo", NULL); ops_undo(); return ok(); }
static Json *op_redo(Json *a, const char *w, void *u) { (void)a; (void)w; (void)u; if (!ops_can_redo()) return fail("nothing to redo", NULL); ops_redo(); return ok(); }

static Json *op_message(Json *a, const char *w, void *u)
{
    (void)u;
    const char *t = json_gets(a, "text", "");
    ed_status(0xB48ACF, "%s: %s", w ? w : "?", t);
    ed_log(LOG_AGENT, "%s says: %s", w ? w : "?", t);
    return ok();
}

static Json *op_mode(Json *a, const char *w, void *u)
{
    (void)w; (void)u;
    static const char *const NAMES[MODE_COUNT] = {"scene", "graph", "build", "git", "agent", "plugins"};
    const char *m = json_gets(a, "mode", "scene");
    for (int i = 0; i < MODE_COUNT; i++)
        if (!strcmp(NAMES[i], m)) { ed.mode = i; return ok(); }
    return fail("mode: scene graph build git agent plugins (got %s)", m);
}

// ---------------------------------------------------------------- table
static void reg(const char *name, int mutates, OpFn fn, const char *help)
{
    if (s_nops >= MAX_OPS) return;
    s_ops[s_nops++] = (Op){name, help, fn, mutates, NULL};
}

static int s_nbuiltin;

static void init_ops(void)
{
    if (s_nops) return;
    reg("help", 0, op_help, "{} -> every operation with its arguments");
    reg("project.get", 0, op_project_get, "{} -> scenes (layers, bottom first), tunnels, lines");
    reg("project.save", 0, op_project_save, "{} -> writes mexnya/*.json and NOTES.md");
    reg("project.reload", 0, op_project_reload, "{} -> re-reads files changed outside the editor");
    reg("scene.create", 1, op_scene_create, "{name, kind: \"3d\"|\"2d\", id?, index?} -> {id}");
    reg("scene.delete", 1, op_scene_delete, "{scene} (also removes its lines)");
    reg("scene.get", 0, op_scene_get, "{scene?} -> the whole scene document");
    reg("scene.set", 1, op_scene_set, "{scene?, fields: {name?, visible?, locked?, settings?: {...}}}");
    reg("scene.move", 1, op_scene_move, "{scene, index} layer order (0 = bottom)");
    reg("scene.select", 0, op_scene_select, "{scene} make it the active scene in the GUI");
    reg("object.add", 1, op_object_add, "{scene?, type, name?, id?, pos?, rot?, scale?, color?, model?, rect?, text?, props?} -> {id}");
    reg("object.set", 1, op_object_set, "{scene?, id, fields: {...}} (null removes a field)");
    reg("object.delete", 1, op_object_delete, "{scene?, id}");
    reg("object.get", 0, op_object_get, "{scene?, id}");
    reg("object.list", 0, op_object_list, "{scene?}");
    reg("object.duplicate", 1, op_object_duplicate, "{scene?, id} -> {id}");
    reg("note.add", 1, op_note_add, "{scene?, kind: box|circle|arrow|pin|pen, text, shape: {rect:[x,y,w,h]} | {circle:[cx,cy,r]} | {arrow:[x0,y0,x1,y1]} | {pin:[x,y]} | {points:[[x,y],...]}, camera?, color?} -> {id, note}");
    reg("note.set", 1, op_note_set, "{scene?, id, fields: {text?, status?, color?, shape?}}");
    reg("note.reply", 1, op_note_reply, "{scene?, id, text}");
    reg("note.resolve", 1, op_note_resolve, "{scene?, id, reply?, reopen?}");
    reg("note.delete", 1, op_note_delete, "{scene?, id}");
    reg("note.list", 0, op_note_list, "{scene?, status?: open|done} -> notes with shapes, targets and image paths");
    reg("tunnel.add", 1, op_tunnel_add, "{name, id?, ports: [\"open\", {name, dir: in|out|both}], note?} -> {id}");
    reg("tunnel.set", 1, op_tunnel_set, "{id, fields}");
    reg("tunnel.delete", 1, op_tunnel_delete, "{id} (also removes its lines)");
    reg("port.add", 1, op_port_add, "{node: scene or tunnel id, name, dir: in|out|both}");
    reg("port.delete", 1, op_port_delete, "{node, name}");
    reg("line.add", 1, op_line_add, "{from: \"node.port\", to: \"node.port\", note?} -> {id}");
    reg("line.set", 1, op_line_set, "{id, fields: {from?, to?, note?}}");
    reg("line.delete", 1, op_line_delete, "{id}");
    reg("graph.layout", 1, op_graph_layout, "{id, pos: [x, y]} node position on the GRAPH page");
    reg("graph.emit", 0, op_graph_emit, "{from: \"node.port\"} -> ports reached through lines and tunnels");
    reg("view.get", 0, op_view_get, "{scene?} -> camera and viewport size");
    reg("view.set", 0, op_view_set, "{scene?, camera?: {target, yaw, pitch, dist, fov, ortho}, preset?: top|front|right|back|left|persp}");
    reg("view.snapshot", 0, op_view_snapshot, "{scene?, camera?, preset?, width?, height?, path?: \"snapshots/x.png\", notes?: true} -> {path} PNG an agent can look at");
    reg("select", 0, op_select, "{scene?, id} select an object in the GUI");
    reg("script.run", 0, op_script_run, "{code} or {file: path inside mexnya/} runs Lua with the mx API -> {output}");
    reg("build.run", 0, op_build_run, "{target: debug|release|windows|all|test|package|package-macos|package-windows}");
    reg("build.status", 0, op_build_status, "{} -> running and the last lines");
    reg("git.status", 0, op_git_status, "{}");
    reg("git.log", 0, op_git_log, "{n?}");
    reg("git.diff", 0, op_git_diff, "{path?}");
    reg("git.commit", 0, op_git_commit, "{message, all?: true} saves, stages and commits");
    reg("git.push", 0, op_git_push, "{}");
    reg("git.pull", 0, op_git_pull, "{} (fast-forward only)");
    reg("git.fetch", 0, op_git_fetch, "{}");
    reg("git.branch", 0, op_git_branch, "{switch?: name}");
    reg("git.init", 0, op_git_init, "{}");
    reg("plugin.list", 0, op_plugin_list, "{}");
    reg("plugin.reload", 0, op_plugin_reload, "{}");
    reg("log.tail", 0, op_log_tail, "{n?, channel?: agent|log|build|lua}");
    reg("undo", 0, op_undo, "{}");
    reg("redo", 0, op_redo, "{}");
    reg("editor.message", 0, op_message, "{text} show a message to the person in the editor");
    reg("editor.mode", 0, op_mode, "{mode: scene|graph|build|git|agent|plugins}");
    s_nbuiltin = s_nops;
}

void ops_reset_plugins(void)
{
    init_ops();
    s_nops = s_nbuiltin;
    s_nplugin = 0;
}

int ops_register(const char *name, const char *help, OpFn fn, void *user)
{
    init_ops();
    if (!name || !strchr(name, '.') || strlen(name) > 63) return -1;  // plugin ops are "<plugin>.<verb>"
    if (s_nops >= MAX_OPS || s_nplugin >= 64 || !name) return -1;
    for (int i = 0; i < s_nops; i++)
        if (!strcmp(s_ops[i].name, name)) {
            if (i < s_nbuiltin) {
                // Built-in operations are the contract with agents: a plugin cannot replace them.
                ed_log(LOG_ERR, "plugin operation \"%s\" refused: it is a built-in operation", name);
                return -1;
            }
            s_ops[i].fn = fn;
            s_ops[i].user = user;
            return 0;
        }
    snprintf(s_plugin_names[s_nplugin], 64, "%s", name);
    snprintf(s_plugin_help[s_nplugin], 200, "%s", help ? help : "");
    s_ops[s_nops++] = (Op){s_plugin_names[s_nplugin], s_plugin_help[s_nplugin], fn, 1, user};
    s_nplugin++;
    return 0;
}

Json *ops_help(void)
{
    init_ops();
    Json *r = ok();
    Json *list = json_set(r, "ops", json_arr());
    for (int i = 0; i < s_nops; i++) {
        Json *o = json_obj();
        json_sets(o, "op", s_ops[i].name);
        json_sets(o, "args", s_ops[i].help);
        json_setb(o, "changes_project", s_ops[i].mutates);
        json_push(list, o);
    }
    json_sets(r, "note", "scene defaults to the active scene. Every call is logged to mexnya/agent/log.jsonl.");
    return r;
}

Json *ops_exec(const Json *req, const char *who)
{
    init_ops();
    double t0 = plat_time();
    const char *name = json_gets(req, "op", NULL);
    Json *args = json_get(req, "args");
    Json *empty = NULL;
    if (!args || args->type != JSON_OBJ) args = empty = json_obj();
    Json *res = NULL;
    const Op *op = NULL;
    for (int i = 0; name && i < s_nops; i++)
        if (!strcmp(s_ops[i].name, name)) { op = &s_ops[i]; break; }
    if (!op) res = fail("unknown op \"%s\" (try \"help\")", name ? name : "");
    else if (!ed.project) res = fail("no project open", NULL);
    else {
        res = op->fn(args, who, op->user);
        if (!res) res = ok();
    }
    int good = json_getb(res, "ok", 0);
    if (good && op && op->mutates) {
        ed.dirty = 1;
        if (who && strcmp(who, "human")) project_save();  // agents read files back at once
        script_emit(name, args);
    }
    int quiet = who && !strcmp(who, "human") && op && !op->mutates;
    if (s_group && op && op->mutates && good) {
        json_free(s_group_req);
        json_free(s_group_res);
        s_group_req = json_clone(req);
        s_group_res = json_clone(res);
        snprintf(s_group_who, sizeof(s_group_who), "%s", who ? who : "?");
        s_group_ms = (plat_time() - t0) * 1000.0;
    } else if (!quiet) log_op(who, req, res, (plat_time() - t0) * 1000.0);
    json_free(empty);
    return res;
}

Json *ops_call(const char *op, Json *args, const char *who)
{
    Json *req = json_obj();
    json_sets(req, "op", op);
    json_set(req, "args", args ? args : json_obj());
    Json *r = ops_exec(req, who);
    json_free(req);
    return r;
}

int ops_do(const char *op, Json *args)
{
    Json *r = ops_call(op, args, ed.agent_name[0] ? ed.agent_name : "human");
    int good = json_getb(r, "ok", 0);
    if (!good) ed_status(0xC0704F, "%s: %s", op, json_gets(r, "error", "failed"));
    json_free(r);
    return good;
}
