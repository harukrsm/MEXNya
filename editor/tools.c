// BUILD and GIT pages, and the background job runner they share. The build goes through
// the engine's own build tool (engine/tools/mexnya.py), so the editor, the terminal and
// agents all compile the same way.
#include "ed.h"
#include "gui.h"
#include "engine/platform/platform.h"
#include "engine/render/font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <sys/wait.h>
#endif

// ---------------------------------------------------------------- jobs
#define QMAX 512
static PlatMutex *s_lock;
static char s_q[QMAX][240];
static int s_qn;
static volatile int s_running;
static volatile int s_exit_code;
static char s_job_title[64];
static char s_job_cmd[ED_PATH * 2];
static int s_job_channel;
static double s_job_t0, s_job_t1;
static int s_progress;

static int exit_status(int st)
{
#ifdef _WIN32
    return st;
#else
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

static int job_thread(void *arg)
{
    (void)arg;
    FILE *f = popen(s_job_cmd, "r");
    if (!f) { s_exit_code = -1; s_running = 0; return 0; }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        // Strip terminal colour codes and carriage-return progress redraws.
        char clean[240];
        int k = 0;
        for (size_t i = 0; i < n && k < (int)sizeof(clean) - 1; i++) {
            if (line[i] == 0x1b) { while (i < n && line[i] != 'm' && line[i] != 'K') i++; continue; }
            if (line[i] == '\r') { k = 0; continue; }
            clean[k++] = line[i];
        }
        clean[k] = 0;
        // The terminal banner is block art; in the proportional pixel font it only smears.
        if (strstr(clean, "\xe2\x96\x88") || strstr(clean, "\xe2\x96\x93") || strstr(clean, "\xe2\x96\x91")) continue;
        plat_mutex_lock(s_lock);
        if (s_qn < QMAX) snprintf(s_q[s_qn++], 240, "%s", clean);
        plat_mutex_unlock(s_lock);
    }
    s_exit_code = exit_status(pclose(f));
    s_running = 0;
    return 0;
}

int job_running(void) { return s_running; }

int job_start(const char *title, const char *cmd, int channel)
{
    if (s_running) return -1;
    if (!s_lock) s_lock = plat_mutex_create("jobs");
    snprintf(s_job_title, sizeof(s_job_title), "%s", title);
    snprintf(s_job_cmd, sizeof(s_job_cmd), "%s", cmd);
    s_job_channel = channel;
    s_job_t0 = plat_time();
    s_progress = 0;
    s_running = 1;
    char head[300];
    snprintf(head, sizeof(head), "$ %s", cmd);
    log_to(channel, LOG_INFO, head);
    PlatThread *t = plat_thread_start("job", job_thread, NULL, 32, 256);
    if (!t) { s_running = 0; return -1; }
    plat_thread_detach(t);
    return 0;
}

void tools_init(void) { if (!s_lock) s_lock = plat_mutex_create("jobs"); }

static int s_was_running;

void tools_update(void)
{
    if (!s_lock) return;
    plat_mutex_lock(s_lock);
    for (int i = 0; i < s_qn; i++) {
        const char *l = s_q[i];
        const char *pc = strstr(l, "%]");
        if (pc && pc - l >= 3) s_progress = atoi(pc - 3);
        const char *pp = strchr(l, '%');
        if (!pc && pp && pp - l >= 3 && strstr(l, "compile")) s_progress = atoi(pp - 3);
        int kind = strstr(l, "error") || strstr(l, "failed") ? LOG_ERR : (strstr(l, "warning") ? LOG_WARN : LOG_INFO);
        log_to(s_job_channel, kind, l);
    }
    s_qn = 0;
    plat_mutex_unlock(s_lock);
    if (s_was_running && !s_running) {
        s_job_t1 = plat_time();
        char t[200];
        snprintf(t, sizeof(t), T("build.result"), s_job_title, s_exit_code == 0 ? T("build.done") : T("build.failed"), s_job_t1 - s_job_t0, s_exit_code);
        log_to(s_job_channel, s_exit_code == 0 ? LOG_OK : LOG_ERR, t);
        ed_status(s_exit_code == 0 ? C_OK : C_WARN, "%s", t);
        ed_log(s_exit_code == 0 ? LOG_OK : LOG_ERR, "%s", t);
    }
    s_was_running = s_running;
}

// The engine's build tool. The editor runs from <engine>/build/<dir>/, a project may
// be anywhere: look in the project, next to the editor, then in $MEXNYA_HOME.
const char *ed_tool_path(void)
{
    static char path[4096];
    char probe[ED_PATH + 64];
    const char *cands[4];
    char a[ED_PATH + 64], b[ED_PATH + 64], c[ED_PATH + 64], d[ED_PATH + 64];
    snprintf(a, sizeof(a), "%s/engine/tools/mexnya.py", ed.root);
    snprintf(b, sizeof(b), "%s/../../engine/tools/mexnya.py", ed.exe_dir);
    snprintf(c, sizeof(c), "%s/../engine/tools/mexnya.py", ed.exe_dir);
    const char *home = getenv("MEXNYA_HOME");
    snprintf(d, sizeof(d), "%s/engine/tools/mexnya.py", home ? home : ".");
    cands[0] = a; cands[1] = b; cands[2] = c; cands[3] = d;
    for (int i = 0; i < 4; i++) {
        snprintf(probe, sizeof(probe), "%s", cands[i]);
        if (plat_file_exists(probe)) {
#ifdef _WIN32
            if (!_fullpath(path, probe, sizeof(path))) snprintf(path, sizeof(path), "%s", probe);
#else
            if (!realpath(probe, path)) snprintf(path, sizeof(path), "%s", probe);
#endif
            return path;
        }
    }
    snprintf(path, sizeof(path), "engine/tools/mexnya.py");
    return path;
}

Json *run_capture(const char *cmd, int max_bytes)
{
    Json *r = json_obj();
    FILE *f = popen(cmd, "r");
    if (!f) { json_setn(r, "code", -1); json_sets(r, "output", "cannot run command"); return r; }
    char *buf = (char *)malloc((size_t)max_bytes + 1);
    int n = 0;
    if (buf) {
        int c;
        while ((c = fgetc(f)) != EOF) if (n < max_bytes) buf[n++] = (char)c;
        buf[n] = 0;
    }
    int code = exit_status(pclose(f));
    json_setn(r, "code", code);
    json_sets(r, "output", buf ? buf : "");
    free(buf);
    return r;
}

static void quote(char *out, int size, const char *s)
{
#ifdef _WIN32
    snprintf(out, (size_t)size, "\"%s\"", s);
#else
    int n = 0;
    out[n++] = '\'';
    for (; *s && n < size - 6; s++) {
        if (*s == '\'') { memcpy(out + n, "'\\''", 4); n += 4; }
        else out[n++] = *s;
    }
    out[n++] = '\'';
    out[n] = 0;
#endif
}

// ---------------------------------------------------------------- BUILD page
static void log_view(int ch, int x, int y, int w, int h, int *scroll)
{
    gui_rect(x, y, w, h, 0x17181B);
    gui_frame(x, y, w, h, C_EDGE_IN);
    int n = log_count(ch), rows = (h - 6) / 16;
    if (gui_hit(x, y, w, h) && gui.in.wheel && !gui.wheel_used) { *scroll += gui.in.wheel * 3; gui.wheel_used = 1; }
    if (*scroll > n - rows) *scroll = n - rows;
    if (*scroll < 0) *scroll = 0;
    int first = n - rows - *scroll;
    if (first < 0) first = 0;
    gui_clip(x + 1, y + 1, w - 2, h - 2);
    for (int i = first, r = 0; i < n && r < rows; i++, r++) {
        int kind;
        const char *t = log_line(ch, i, &kind);
        unsigned int c = kind == LOG_ERR ? C_WARN : (kind == LOG_WARN ? C_ACCENT : (kind == LOG_OK ? C_OK : C_TEXT));
        if (t[0] == '$') c = C_COLD;
        gui_text_clip(x + 6, y + 3 + r * 16, w - 12, c, t);
    }
    gui_unclip();
}

static Json *s_cfg;

static void load_cfg(void)
{
    if (s_cfg) return;
    char path[ED_PATH];
    snprintf(path, sizeof(path), "%s/mexnya.json", ed.root);
    s_cfg = json_load(path, NULL, 0);
    if (!s_cfg) s_cfg = json_obj();
}

static void start_build(const char *target)
{
    Json *a = json_obj();
    json_sets(a, "target", target);
    ops_do("build.run", a);
}

void build_page(int x, int y, int w, int h)
{
    static int scroll;
    load_cfg();
    gui_rect(x, y, w, h, C_PANEL);
    int lw = 230;
    gui_header(x, y, lw, T("tab.build"));
    int cy = y + 26;
    font_draw(x + 10, cy, C_DIM, T("build.targets"));
    cy += 20;
    Json *targets = json_get(s_cfg, "targets");
    for (Json *t = targets ? targets->child : NULL; t; t = t->next) {
        char label[80];
        snprintf(label, sizeof(label), T("build.target"), t->key);
        if (gui_button_id(gui_id(t->key, 0xB17D), label, x + 10, cy, lw - 20, 20, 0) && !job_running()) start_build(t->key);
        font_draw(x + lw - 10 - font_width(json_gets(t, "type", "")) - 6, cy + 1, C_DIM, json_gets(t, "type", ""));
        cy += 24;
    }
    if (gui_button("build.all", x + 10, cy, lw - 20, 20) && !job_running()) start_build("all");
    cy += 30;
    font_draw(x + 10, cy, C_DIM, T("build.test_package"));
    cy += 20;
    if (gui_button("build.test", x + 10, cy, lw - 20, 20) && !job_running()) start_build("test");
    cy += 24;
    Json *pk = json_get(s_cfg, "packages");
    for (Json *p = pk ? pk->child : NULL; p; p = p->next) {
        char label[80], target[80];
        snprintf(label, sizeof(label), T("build.package"), p->key);
        snprintf(target, sizeof(target), "package-%s", p->key);
        if (gui_button_id(gui_id(target, 0xB17E), label, x + 10, cy, lw - 20, 20, 0) && !job_running()) start_build(target);
        cy += 24;
    }
    cy += 6;
    font_draw(x + 10, cy, C_DIM, T("build.run"));
    cy += 20;
    Json *run = json_get(json_get(s_cfg, "editor"), "run");
    const char *bin = json_gets(run, "binary", NULL);
    if (bin && gui_button("build.run_game", x + 10, cy, lw - 20, 20)) {
        char cmd[ED_PATH * 2], q[ED_PATH + 8], exe[ED_PATH];
        Json *t = json_get(targets, json_gets(run, "target", "debug"));
        snprintf(exe, sizeof(exe), "%s/%s/%s", ed.root, json_gets(t, "dir", "build/desktop"), bin);
        quote(q, sizeof(q), exe);
#ifdef _WIN32
        snprintf(cmd, sizeof(cmd), "start \"\" %s", q);
#else
        snprintf(cmd, sizeof(cmd), "%s > /dev/null 2>&1 &", q);
#endif
        if (system(cmd) == 0) ed_status(C_OK, T("build.started"), bin);
    }
    if (bin) cy += 24;
    if (gui_button("build.doctor", x + 10, cy, lw - 20, 20) && !job_running()) {
        char cmd[ED_PATH * 2], q[ED_PATH + 8];
        quote(q, sizeof(q), ed.root);
        char tq[ED_PATH + 72];
        quote(tq, sizeof(tq), ed_tool_path());
        snprintf(cmd, sizeof(cmd), "cd %s && python3 %s doctor 2>&1", q, tq);
        job_start("doctor", cmd, CH_BUILD);
    }
    cy += 32;
    // Progress.
    if (job_running()) {
        font_drawf(x + 10, cy, C_ACCENT, "%s  %.0fs", s_job_title, plat_time() - s_job_t0);
        cy += 20;
        gui_rect(x + 10, cy, lw - 20, 10, C_BG);
        gui_rect(x + 10, cy, (lw - 20) * s_progress / 100, 10, C_ACCENT);
        gui_frame(x + 10, cy, lw - 20, 10, C_EDGE);
    } else if (s_job_title[0]) {
        font_drawf(x + 10, cy, s_exit_code == 0 ? C_OK : C_WARN, T("build.state"), s_job_title, s_exit_code == 0 ? T("build.done") : T("build.failed"));
    }
    gui_rect(x + lw, y, 1, h, C_EDGE);
    gui_header(x + lw + 1, y, w - lw - 1, T("build.output"));
    log_view(CH_BUILD, x + lw + 8, y + 24, w - lw - 16, h - 32, &scroll);
}

// ---------------------------------------------------------------- GIT page
static Json *s_status, *s_log, *s_diff;
static double s_git_t;
static char s_msg[512];
static char s_diff_path[ED_PATH];
static double s_push_arm;

static Json *git(const char *args)
{
    char cmd[ED_PATH * 3], q[ED_PATH + 8];
    quote(q, sizeof(q), ed.root);
    snprintf(cmd, sizeof(cmd), "git -C %s %s 2>&1", q, args);
    return run_capture(cmd, 1 << 18);
}

static void git_refresh(void)
{
    json_free(s_status);
    json_free(s_log);
    s_status = git("status --porcelain=v1 --branch");
    s_log = git("log --oneline --decorate -n 30");
    s_git_t = ed.time;
}

static void git_job(const char *title, const char *args)
{
    char cmd[ED_PATH * 3], q[ED_PATH + 8];
    quote(q, sizeof(q), ed.root);
    snprintf(cmd, sizeof(cmd), "git -C %s %s 2>&1", q, args);
    job_start(title, cmd, CH_BUILD);
    ed.bottom_tab = CH_BUILD;
}

static void lines_of(const char *text, void (*fn)(const char *line, int len, int i, void *u), void *u)
{
    int i = 0;
    while (text && *text) {
        int n = 0;
        while (text[n] && text[n] != '\n') n++;
        fn(text, n, i++, u);
        text += n;
        if (*text) text++;
    }
}

typedef struct { int x, y, w, h, row, first; } ListCtx;

static void status_line(const char *l, int n, int i, void *u)
{
    ListCtx *c = (ListCtx *)u;
    if (i == 0 && n > 2 && l[0] == '#') return;  // branch line, shown in the header
    int r = c->row++;
    if (r < c->first) return;
    int ry = c->y + (r - c->first) * 17;
    if (ry + 17 > c->y + c->h) return;
    char path[ED_PATH];
    snprintf(path, sizeof(path), "%.*s", n > 3 ? n - 3 : 0, l + 3);
    GuiId id = gui_id(path, 0x617);
    if (gui_row(id, c->x, ry, c->w, 17, !strcmp(path, s_diff_path))) {
        snprintf(s_diff_path, sizeof(s_diff_path), "%s", path);
        json_free(s_diff);
        char args[ED_PATH + 64], q[ED_PATH + 8];
        quote(q, sizeof(q), path);
        snprintf(args, sizeof(args), "diff --no-color -- %s", q);
        s_diff = git(args);
        if (!json_gets(s_diff, "output", "")[0]) {
            json_free(s_diff);
            snprintf(args, sizeof(args), "diff --no-color --cached -- %s", q);
            s_diff = git(args);
        }
    }
    char code[3] = {l[0], l[1], 0};
    unsigned int col = code[0] == '?' ? C_COLD : (code[0] == 'M' || code[1] == 'M' ? C_ACCENT : (code[0] == 'D' || code[1] == 'D' ? C_WARN : C_OK));
    font_draw(c->x + 6, ry, col, code);
    gui_text_clip(c->x + 30, ry, c->w - 36, C_TEXT, path);
}

static void text_line(const char *l, int n, int i, void *u)
{
    ListCtx *c = (ListCtx *)u;
    if (i < c->first) return;
    int ry = c->y + (i - c->first) * 16;
    if (ry + 16 > c->y + c->h) return;
    char t[300];
    snprintf(t, sizeof(t), "%.*s", n < 290 ? n : 290, l);
    unsigned int col = C_TEXT;
    if (t[0] == '+' && t[1] != '+') col = C_OK;
    else if (t[0] == '-' && t[1] != '-') col = C_WARN;
    else if (t[0] == '@') col = C_COLD;
    gui_text_clip(c->x, ry, c->w, col, t);
}

void git_page(int x, int y, int w, int h)
{
    static int s1, s2;
    if (!s_status || ed.time - s_git_t > 6.0) git_refresh();
    gui_rect(x, y, w, h, C_PANEL);
    const char *st = json_gets(s_status, "output", "");
    char branch[200] = "?";
    if (!strncmp(st, "## ", 3)) {
        int n = 0;
        while (st[3 + n] && st[3 + n] != '\n' && n < 190) n++;
        snprintf(branch, sizeof(branch), "%.*s", n, st + 3);
    }
    int not_repo = json_getn(s_status, "code", 0) != 0;
    int lw = w * 45 / 100;
    gui_header(x, y, lw, T("git.title"));
    int cy = y + 24;
    if (not_repo) {
        font_draw(x + 10, cy, C_WARN, T("git.not_repo"));
        if (gui_button("git.init", x + 10, cy + 22, 100, 20)) { json_free(ops_call("git.init", NULL, "human")); git_refresh(); }
        return;
    }
    font_draw(x + 10, cy, C_DIM, T("git.branch"));
    gui_text_clip(x + 46, cy, lw - 56, C_ACCENT, branch);
    cy += 22;
    int bw = (lw - 20 - 12) / 4;
    if (gui_button("git.refresh", x + 10, cy, bw, 20)) git_refresh();
    if (gui_button("git.fetch", x + 14 + bw, cy, bw, 20) && !job_running()) git_job("git fetch", "fetch --all --prune");
    if (gui_button("git.pull", x + 18 + bw * 2, cy, bw, 20) && !job_running()) git_job("git pull", "pull --ff-only");
    int armed = ed.time - s_push_arm < 3.0;
    if (gui_button_id(gui_id("git-push", 0), armed ? T("git.confirm_push") : T("git.push"), x + 22 + bw * 3, cy, bw, 20, armed) && !job_running()) {
        if (armed) { git_job("git push", "push"); s_push_arm = 0; }
        else { s_push_arm = ed.time; ed_status(C_ACCENT, "%s", T("git.push_hint")); }
    }
    cy += 28;
    font_draw(x + 10, cy, C_DIM, T("git.changes"));
    cy += 18;
    int list_h = h - (cy - y) - 76;
    ListCtx c = {x + 4, cy, lw - 8, list_h, 0, s1};
    gui_rect(x + 4, cy, lw - 8, list_h, 0x1B1C1F);
    gui_clip(x + 4, cy, lw - 8, list_h);
    lines_of(st, status_line, &c);
    gui_unclip();
    if (gui_hit(x + 4, cy, lw - 8, list_h) && gui.in.wheel && !gui.wheel_used) { s1 -= gui.in.wheel * 2; gui.wheel_used = 1; }
    if (s1 < 0) s1 = 0;
    if (s1 > c.row - 1) s1 = c.row > 0 ? c.row - 1 : 0;
    if (!c.row) font_draw(x + 12, cy + 4, C_OK, T("git.clean"));
    cy += list_h + 8;
    font_draw(x + 10, cy, C_DIM, T("git.message"));
    cy += 18;
    gui_field(gui_id("git-msg", 0), x + 10, cy, lw - 20, s_msg, sizeof(s_msg));
    cy += 22;
    if (gui_button("git.commit_all", x + 10, cy, lw - 20, 20)) {
        if (!s_msg[0]) ed_status(C_WARN, "%s", T("git.need_message"));
        else {
            Json *a = json_obj();
            json_sets(a, "message", s_msg);
            Json *r = ops_call("git.commit", a, "human");
            const char *out = json_gets(r, "output", "");
            log_to(CH_BUILD, json_getb(r, "ok", 0) ? LOG_OK : LOG_ERR, out[0] ? out : json_gets(r, "error", ""));
            ed_status(json_getb(r, "ok", 0) ? C_OK : C_WARN, "%s", json_getb(r, "ok", 0) ? T("git.committed") : T("git.commit_failed"));
            if (json_getb(r, "ok", 0)) s_msg[0] = 0;
            json_free(r);
            git_refresh();
        }
    }
    // Right: diff of the picked file, or the history.
    int rx = x + lw + 1, rw = w - lw - 1;
    gui_rect(rx - 1, y, 1, h, C_EDGE);
    gui_header(rx, y, rw, s_diff_path[0] ? s_diff_path : T("git.history"));
    if (s_diff_path[0] && gui_button("git.history", rx + rw - 52, y + 1, 48, 15)) { s_diff_path[0] = 0; s2 = 0; }
    const char *text = s_diff_path[0] ? json_gets(s_diff, "output", "") : json_gets(s_log, "output", "");
    if (s_diff_path[0] && !text[0]) text = T("git.no_diff");
    ListCtx d = {rx + 8, y + 24, rw - 16, h - 32, 0, s2};
    if (gui_hit(rx, y + 20, rw, h - 20) && gui.in.wheel && !gui.wheel_used) { s2 -= gui.in.wheel * 3; gui.wheel_used = 1; }
    if (s2 < 0) s2 = 0;
    gui_clip(rx, y + 20, rw, h - 20);
    lines_of(text, text_line, &d);
    gui_unclip();
}
