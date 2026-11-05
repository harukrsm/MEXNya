// MEXNya Editor: shared state. The editor is written on the engine it edits: the same
// renderer, font, input and platform layer, plus engine/core JSON for every file.
//
// Everything that changes the project goes through ops.c (the operation layer): the
// GUI, Lua scripts, plugins and outside agents all call the same named operations,
// and each one is appended to mexnya/agent/log.jsonl with who did it.
#ifndef ED_ED_H
#define ED_ED_H

#include "engine/core/json.h"
#include "engine/core/i18n.h"
#include "engine/platform/input.h"
#include "engine/render/render_config.h"

#define ED_VERSION "0.1"
// User-facing text is always tr("key"); keys, identifiers and data stay English.
#define T(key) tr(key)
// The window in UI points (pixels / ui_scale); changes when the window is resized.
#define ED_W (ed.w)
#define ED_H (ed.h)
#define ED_MAX_SCENES 64
#define ED_PATH 1024

enum { MODE_SCENE = 0, MODE_GRAPH, MODE_BUILD, MODE_GIT, MODE_AGENT, MODE_PLUGINS, MODE_COUNT };

enum {
    TOOL_SELECT = 0, TOOL_MOVE, TOOL_ROTATE, TOOL_SCALE,
    TOOL_BOX, TOOL_CIRCLE, TOOL_ARROW, TOOL_PIN, TOOL_PEN, TOOL_COUNT
};

typedef struct {
    float target[3];
    float yaw, pitch;   // degrees
    float dist;
    float fov;
    int ortho;          // 0 perspective, 1 orthographic
} Cam;

// The scene viewport's screen rectangle and camera for this frame (for projection).
typedef struct {
    int x, y, w, h;
    Cam cam;
    float eye[3], fwd[3], right[3], up[3];
    float tan_half, aspect, ortho_h;
    // 2D scenes: canvas -> screen mapping
    float cx, cy, cscale;
    int canvas_w, canvas_h;
} View;

typedef struct {
    char root[ED_PATH];       // project root (mexnya.json lives here)
    char dir[ED_PATH];        // root/mexnya
    char exe_dir[ED_PATH];
    Json *project;            // mexnya/project.json
    Json *scenes[ED_MAX_SCENES];
    int nscenes;
    int cur;                  // active scene index or -1
    char sel[64];             // selected object id in the active scene
    char sel_note[64];        // selected note id in the active scene
    char sel_node[64];        // graph: selected scene/tunnel id
    char sel_line[64];        // graph: selected line id
    int tool, mode;
    int dirty;
    int headless;
    Input in;
    float dt;
    double time;
    Cam cams[ED_MAX_SCENES];
    View view;
    char status[256];
    unsigned int status_rgb;
    double status_t;
    char agent_name[64];      // who the GUI acts as in the log ("human")
    int bottom_tab;
    int snapshot_pending;     // capture the viewport at the end of this frame
    char snapshot_path[ED_PATH];
    char snapshot_note[64];
    int quit;
    int w, h;                 // window size in UI points
    int ui_scale;             // pixels per UI point (2 on a Retina screen)
    char language[16];        // UI language code ("en", "zh-Hans", ...): display only
    int font_pref;            // FONT_PIXEL or FONT_SYSTEM (requested)
    int settings_open;
} Ed;

extern Ed ed;

// ---------------------------------------------------------------- project (project.c)
int project_open(const char *root);   // creates mexnya/ with a starter project if missing
int project_save(void);
void project_close(void);
Json *scene_at(int i);
int scene_index(const char *id);
Json *scene_by_id(const char *id);
Json *cur_scene(void);
const char *scene_id(const Json *s);
int scene_is_2d(const Json *s);
Json *scene_objects(Json *s);
Json *scene_notes(Json *s);
Json *find_object(Json *s, const char *id);
Json *find_note(Json *s, const char *id);
void make_id(char *out, int size, const char *prefix, const Json *taken_in);
void ed_path(char *out, int size, const char *rel);     // root/mexnya/<rel>
void cam_from_json(Cam *c, const Json *j);
Json *cam_to_json(const Cam *c);
void cam_default(Cam *c);
unsigned int parse_color(const char *s, unsigned int def);
void format_color(char *out, int size, unsigned int rgb);
void notes_export_md(void);                  // mexnya/NOTES.md
void ed_now_iso(char *out, int size);

// ---------------------------------------------------------------- operations (ops.c)
// req: {"op": "...", "args": {...}}. Returns {"ok": true, ...} or {"ok": false, "error": ...}.
// The caller frees the result. `who` is written to the log ("human", "my-agent", "lua", ...).
Json *ops_exec(const Json *req, const char *who);
// Convenience: op name + args (args is consumed).
Json *ops_call(const char *op, Json *args, const char *who);
// Same, and frees the result; returns 1 if ok.
int ops_do(const char *op, Json *args);
void ops_undo(void);
void ops_redo(void);
int ops_can_undo(void);
int ops_can_redo(void);
Json *ops_help(void);
typedef Json *(*OpFn)(Json *args, const char *who, void *user);
int ops_register(const char *name, const char *help, OpFn fn, void *user);  // plugins
// Begin/end a group: every change in between undoes as one step (drags).
void ops_group_begin(void);
void ops_group_end(void);
int ops_in_group(void);
void ops_reset_plugins(void);   // plugins are reloading: drop their operations
void agent_shutdown_beat(void);
Json *scene_new(const char *id, const char *name, const char *kind);
int project_reload(void);
int project_changed_on_disk(void);
extern int ed_in_frame;
void snapshots_process(void);

// ---------------------------------------------------------------- log (log.c)
enum { LOG_INFO = 0, LOG_OK, LOG_WARN, LOG_ERR, LOG_AGENT, LOG_BUILD, LOG_LUA };
void ed_log(int kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void ed_status(unsigned int rgb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void log_op(const char *who, const Json *req, const Json *res, double ms);
int log_count(int channel);
const char *log_line(int channel, int i, int *kind);
enum { CH_LOG = 0, CH_AGENT, CH_BUILD, CH_LUA, CH_COUNT };
void log_to(int channel, int kind, const char *line);

// ---------------------------------------------------------------- views
void view3d_frame(int x, int y, int w, int h);   // scene view (3D or 2D active scene)
void view3d_new_frame(void);                    // next 3D draw clears depth
void view3d_setup(View *v, const Cam *c, int x, int y, int w, int h);
int world_to_screen(const View *v, const float p[3], float *sx, float *sy);
void screen_ray(const View *v, float sx, float sy, float org[3], float dir[3]);
void draw_scene_3d(const View *v, Json *scene, int dim);
void draw_scene_2d(const View *v, Json *scene, int editing);
void view2d_fit(View *v, Json *scene, int x, int y, int w, int h, float zoom, float panx, float pany);
// Objects whose projected bounds touch the screen rect (ids appended to arr).
void objects_in_rect(const View *v, Json *scene, float x0, float y0, float x1, float y1, Json *arr);
int pick_object(const View *v, Json *scene, float sx, float sy, char *out_id, int size, float hit[3]);
void view_frame_selected(void);

// notes.c
void notes_tool_input(const View *v, int vx, int vy, int vw, int vh);
void notes_draw(const View *v, Json *scene, int for_snapshot, const char *highlight);
int notes_editing(void);
void notes_popup(void);
int render_snapshot(Json *scene, const Cam *cam, int w, int h, const char *highlight, const char *path);

// panels.c
void panels_left(int x, int y, int w, int h);
void panels_right(int x, int y, int w, int h);
void panel_bottom(int x, int y, int w, int h);
void toolbar(int x, int y, int w, int h);

// graph.c
void graph_frame(int x, int y, int w, int h);

// tools.c: build, git, jobs
void tools_init(void);
void tools_update(void);
void build_page(int x, int y, int w, int h);
void git_page(int x, int y, int w, int h);
int job_start(const char *title, const char *cmd, int channel);
int job_running(void);
Json *run_capture(const char *cmd, int max_bytes);   // {"code": n, "output": "..."}
const char *ed_tool_path(void);                      // engine/tools/mexnya.py, wherever it is

// agent.c
void agent_init(void);
int agent_update(void);   // requests handled this call
void agent_page(int x, int y, int w, int h);
void agent_write_guide(void);

// script.c (Lua) and plugins
int script_init(void);
void script_shutdown(void);
Json *script_run(const char *code, const char *chunk);   // {"ok", "output"}
int script_start(const char *code, const char *chunk);   // coroutine across frames
int script_running(void);
void script_tick(void);
void script_emit(const char *event, const Json *data);   // mx.on(event) handlers
void plugins_load(void);
void plugins_page(int x, int y, int w, int h);
Json *plugins_list(void);

#endif
