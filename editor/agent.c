// The agent bridge. Any agent (an AI coding assistant, a script, another tool) talks to the
// editor through files, so nothing has to be installed or authorised:
//
//   mexnya/agent/inbox/<id>.json   {"agent": "my-agent", "op": "object.add", "args": {...}}
//                                  (or {"agent": ..., "batch": [{op, args}, ...]})
//   mexnya/agent/outbox/<id>.json  the result
//   mexnya/agent/log.jsonl         every operation by anyone, one JSON per line
//   mexnya/agent/editor.json       heartbeat: the GUI is running (pid, time)
//
// `mexnya agent <op> '<json>'` wraps this; without a running editor it runs the
// operation headless. Requests are accepted unconditionally; the log is the record.
#include "ed.h"
#include "gui.h"
#include "engine/platform/platform.h"
#include "engine/render/font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

typedef struct {
    char name[64];
    int ops, errors;
    double last;
} AgentSeen;

static AgentSeen s_seen[16];
static int s_nseen;
static double s_poll_t, s_beat_t;
static int s_total;

static void seen(const char *name, int ok)
{
    for (int i = 0; i < s_nseen; i++)
        if (!strcmp(s_seen[i].name, name)) { s_seen[i].ops++; s_seen[i].errors += !ok; s_seen[i].last = ed.time; return; }
    if (s_nseen < 16) {
        snprintf(s_seen[s_nseen].name, 64, "%s", name);
        s_seen[s_nseen].ops = 1;
        s_seen[s_nseen].errors = !ok;
        s_seen[s_nseen].last = ed.time;
        s_nseen++;
    }
}

void agent_write_guide(void)
{
    char path[ED_PATH];
    ed_path(path, sizeof(path), "AGENTS.md");
    Json *help = ops_help();
    size_t cap = 1 << 15, n = 0;
    char *b = (char *)malloc(cap);
    if (!b) { json_free(help); return; }
#define PUT(...) do { int k_ = snprintf(b + n, cap - n, __VA_ARGS__); \
        if (k_ > 0 && n + (size_t)k_ + 1 >= cap) { cap = (cap + (size_t)k_) * 2; char *nb_ = (char *)realloc(b, cap); if (!nb_) { free(b); json_free(help); return; } b = nb_; k_ = snprintf(b + n, cap - n, __VA_ARGS__); } \
        if (k_ > 0) n += (size_t)k_; } while (0)
    PUT("# MEXNya Editor: guide for AI agents\n\n");
    PUT("This folder is a MEXNya project. A person edits it in the MEXNya Editor (`mexnya run`);\n"
        "you work on the same project through operations. Everything you do is logged in\n"
        "`mexnya/agent/log.jsonl` with your name, and the person sees it live in the editor.\n\n");
    PUT("## Read first\n\n"
        "- `mexnya/NOTES.md`: what the person marked and asked for. Each note has a shape in\n"
        "  pixels, the objects inside it (`targets`), a world point and a PNG snapshot.\n"
        "  Open the PNG to see exactly what was circled.\n"
        "- `mexnya/project.json`: scenes (layers, bottom first), tunnels, lines.\n"
        "- `mexnya/scenes/<id>.json`: one scene with its objects and notes.\n\n");
    PUT("## Concepts\n\n"
        "- **SCENE**: one place: a 3D world or a 2D UI screen. Scenes stack like layers.\n"
        "- **Line**: `from: \"scene.port\"` -> `to: \"scene.port\"`: when a port fires, the next fires.\n"
        "- **Tunnel**: an abstraction layer (a bus). Scenes connect to a tunnel, not to each other.\n"
        "- **Plugin**: `mexnya/plugins/<name>/plugin.json` + Lua or a native library; plugins add operations.\n"
        "- **Note**: a person's (or your) mark on a scene: box, circle, arrow, pin, pen + text.\n\n");
    const char *tool = ed_tool_path();
    PUT("## How to call an operation\n\n"
        "Run from the project folder (where mexnya.json is). `mexnya` below is\n"
        "`python3 %s`.\n\n```bash\n", tool);
    PUT("mexnya agent project.get\n"
        "mexnya agent note.list '{\"status\": \"open\"}'\n"
        "mexnya agent object.set '{\"scene\": \"main\", \"id\": \"player\", \"fields\": {\"pos\": [2, 0.5, -1]}}'\n"
        "mexnya agent view.snapshot '{\"scene\": \"main\", \"preset\": \"top\"}'\n"
        "mexnya agent note.resolve '{\"scene\": \"main\", \"id\": \"note_1\", \"reply\": \"moved it\"}'\n"
        "```\n\n");
    PUT(
        "Set `MEXNYA_AGENT=<your name>` (or pass `--as <name>`) so the log shows who you are.\n"
        "With the editor open the call goes through `mexnya/agent/inbox/` and the person sees it\n"
        "happen; without it the same operation runs headless on the files. Both save at once.\n\n"
        "Raw protocol: write `mexnya/agent/inbox/<id>.json` with `{\"agent\", \"op\", \"args\"}` or\n"
        "`{\"agent\", \"batch\": [{\"op\", \"args\"}, ...]}`; read `mexnya/agent/outbox/<id>.json`.\n\n"
        "Lua: `script.run {\"code\": \"...\"}` runs with the `mx` API (`mx.op(name, args)`, `mx.log`).\n\n");
    PUT("## Working on notes\n\n"
        "1. `note.list {\"status\": \"open\"}` and look at each `image`.\n"
        "2. Change the scene with operations (or edit code for game logic).\n"
        "3. `note.resolve {id, reply}` to tell the person what you did. Ask with `note.reply`.\n"
        "4. You can mark things for the person too: `note.add` with a shape.\n\n");
    PUT("## Operations\n\n| op | args |\n|---|---|\n");
    Json *ops = json_get(help, "ops");
    for (Json *o = ops ? ops->child : NULL; o; o = o->next) {
        char args[600];
        snprintf(args, sizeof(args), "%s", json_gets(o, "args", ""));
        for (char *p = args; *p; p++) if (*p == '|') *p = '/';
        PUT("| `%s` | %s |\n", json_gets(o, "op", ""), args);
    }
    PUT("\n`scene` defaults to the scene that is active in the editor. Coordinates: 3D in metres\n"
        "(y up), `rot` in degrees [x, y, z]; 2D `rect` [x, y, w, h] in canvas pixels.\n");
#undef PUT
    plat_write_file(path, b, (int)n);
    free(b);
    json_free(help);
}

void agent_init(void)
{
    char path[ED_PATH];
    ed_path(path, sizeof(path), "agent/inbox");
    plat_mkdirs(path);
    ed_path(path, sizeof(path), "agent/outbox");
    plat_mkdirs(path);
    agent_write_guide();
}

typedef struct { char names[64][128]; int n; } Inbox;

static int inbox_cb(const char *name, int is_dir, void *user)
{
    Inbox *b = (Inbox *)user;
    size_t l = strlen(name);
    if (is_dir || l < 6 || strcmp(name + l - 5, ".json") || b->n >= 64) return 0;
    snprintf(b->names[b->n++], 128, "%s", name);
    return 0;
}

static int cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void heartbeat(void)
{
    char path[ED_PATH];
    ed_path(path, sizeof(path), "agent/editor.json");
    char t[300];
    snprintf(t, sizeof(t), "{\"pid\": %d, \"time\": %lld, \"version\": \"%s\", \"scene\": \"%s\"}\n",
             (int)getpid(), (long long)time(NULL), ED_VERSION, scene_id(cur_scene()));
    plat_write_file(path, t, (int)strlen(t));
}

void agent_shutdown_beat(void)
{
    char path[ED_PATH];
    ed_path(path, sizeof(path), "agent/editor.json");
    plat_remove_file(path);
}

static Json *handle(Json *req, const char *fallback_who)
{
    const char *who = json_gets(req, "agent", fallback_who);
    Json *batch = json_get(req, "batch");
    if (batch && batch->type == JSON_ARR) {
        Json *r = json_obj();
        json_setb(r, "ok", 1);
        Json *results = json_set(r, "results", json_arr());
        for (Json *q = batch->child; q; q = q->next) {
            Json *one = ops_exec(q, who);
            if (!json_getb(one, "ok", 0)) json_setb(r, "ok", 0);
            seen(who, json_getb(one, "ok", 0));
            free(one->key);
            one->key = NULL;
            json_push(results, one);
        }
        return r;
    }
    Json *r = ops_exec(req, who);
    seen(who, json_getb(r, "ok", 0));
    return r;
}

int agent_update(void)
{
    if (ed.time - s_beat_t > 1.0) { heartbeat(); s_beat_t = ed.time; }
    if (ed.time - s_poll_t < 0.15) return 0;
    s_poll_t = ed.time;
    char dir[ED_PATH];
    ed_path(dir, sizeof(dir), "agent/inbox");
    Inbox b;
    b.n = 0;
    plat_dir_list(dir, inbox_cb, &b);
    if (!b.n) return 0;
    qsort(b.names, (size_t)b.n, sizeof(b.names[0]), cmp);
    for (int i = 0; i < b.n; i++) {
        char in[ED_PATH], out[ED_PATH], id[128], err[200];
        snprintf(in, sizeof(in), "%s/%s", dir, b.names[i]);
        snprintf(id, sizeof(id), "%.*s", (int)strlen(b.names[i]) - 5, b.names[i]);
        Json *req = json_load(in, err, sizeof(err));
        if (!req && plat_file_size(in) <= 0) continue;  // still being written
        Json *res;
        if (!req) {
            res = json_obj();
            json_setb(res, "ok", 0);
            json_sets(res, "error", err);
        } else res = handle(req, "agent");
        json_sets(res, "request", id);
        char rel[200];
        snprintf(rel, sizeof(rel), "agent/outbox/%s.json", id);
        ed_path(out, sizeof(out), rel);
        json_save(out, res);
        plat_remove_file(in);
        json_free(req);
        json_free(res);
        s_total++;
    }
    return b.n;
}

// ---------------------------------------------------------------- page
void agent_page(int x, int y, int w, int h)
{
    static int scroll;
    gui_rect(x, y, w, h, C_PANEL);
    int lw = 300;
    gui_header(x, y, lw, T("tab.agent"));
    int cy = y + 26;
    font_draw(x + 10, cy, C_DIM, T("agent.command_line"));
    cy += 20;
    static const char *const CMDS[] = {
        "mexnya agent help",
        "mexnya agent note.list '{\"status\":\"open\"}'",
        "mexnya agent view.snapshot",
    };
    for (int i = 0; i < 3; i++) {
        gui_rect(x + 10, cy, lw - 20, 18, 0x17181B);
        gui_text_clip(x + 14, cy, lw - 28, C_COLD, CMDS[i]);
        cy += 21;
    }
    cy += 4;
    if (gui_button("agent.copy_setup", x + 10, cy, lw - 20, 20)) {
        char p[ED_PATH], t[ED_PATH * 2 + 200];
        ed_path(p, sizeof(p), "AGENTS.md");
        snprintf(t, sizeof(t), "Read %s first, then use `python3 %s agent <op> '<json>'` to work in the MEXNya editor.", p, ed_tool_path());
        input_clipboard_set(t);
        ed_status(C_OK, "%s", T("agent.copied"));
    }
    cy += 26;
    if (gui_button("agent.regen", x + 10, cy, lw - 20, 20)) { agent_write_guide(); ed_status(C_OK, "%s", T("agent.regenerated")); }
    cy += 32;
    font_drawf(x + 10, cy, C_DIM, T("agent.requests"), s_total);
    cy += 20;
    for (int i = 0; i < s_nseen; i++) {
        AgentSeen *a = &s_seen[i];
        int fresh = ed.time - a->last < 5.0;
        gui_disc((float)(x + 16), (float)(cy + 8), 3, fresh ? C_OK : C_DIM);
        font_drawf(x + 26, cy, C_AGENT, "%s", a->name);
        font_drawf(x + 140, cy, C_TEXT, "%d", a->ops);
        if (a->errors) font_drawf(x + 200, cy, C_WARN, T("agent.errors"), a->errors);
        cy += 18;
    }
    if (!s_nseen) font_draw(x + 10, cy, C_DIM, T("agent.none"));
    gui_rect(x + lw, y, 1, h, C_EDGE);
    gui_header(x + lw + 1, y, w - lw - 1, T("agent.log"));
    int lx = x + lw + 8, ly = y + 24, lwid = w - lw - 16, lh = h - 32;
    gui_rect(lx, ly, lwid, lh, 0x17181B);
    int n = log_count(CH_AGENT), rows = (lh - 6) / 16;
    if (gui_hit(lx, ly, lwid, lh) && gui.in.wheel && !gui.wheel_used) { scroll += gui.in.wheel * 3; gui.wheel_used = 1; }
    if (scroll > n - rows) scroll = n - rows;
    if (scroll < 0) scroll = 0;
    int first = n - rows - scroll;
    if (first < 0) first = 0;
    gui_clip(lx, ly, lwid, lh);
    for (int i = first, r = 0; i < n && r < rows; i++, r++) {
        int kind;
        const char *t = log_line(CH_AGENT, i, &kind);
        gui_text_clip(lx + 6, ly + 3 + r * 16, lwid - 12, kind == LOG_ERR ? C_WARN : (kind == LOG_AGENT ? C_AGENT : C_TEXT), t);
    }
    gui_unclip();
}
