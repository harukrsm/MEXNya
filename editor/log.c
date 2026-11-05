// Editor log channels (Log / Agent / Build / Lua tabs) and the operation log on disk.
#include "ed.h"
#include "engine/platform/platform.h"
#include "gui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINES 400
#define LEN 240

typedef struct {
    char text[LINES][LEN];
    unsigned char kind[LINES];
    int head, count;
} Channel;

static Channel s_ch[CH_COUNT];

void log_to(int channel, int kind, const char *line)
{
    if (channel < 0 || channel >= CH_COUNT) return;
    Channel *c = &s_ch[channel];
    int i = (c->head + c->count) % LINES;
    if (c->count == LINES) c->head = (c->head + 1) % LINES;
    else c->count++;
    snprintf(c->text[i], LEN, "%s", line);
    c->kind[i] = (unsigned char)kind;
    if (ed.headless && channel != CH_AGENT) fprintf(stderr, "%s\n", line);
}

int log_count(int channel) { return channel >= 0 && channel < CH_COUNT ? s_ch[channel].count : 0; }

const char *log_line(int channel, int i, int *kind)
{
    Channel *c = &s_ch[channel];
    if (i < 0 || i >= c->count) return "";
    int k = (c->head + i) % LINES;
    if (kind) *kind = c->kind[k];
    return c->text[k];
}

void ed_log(int kind, const char *fmt, ...)
{
    char buf[LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_to(CH_LOG, kind, buf);
}

void ed_status(unsigned int rgb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ed.status, sizeof(ed.status), fmt, ap);
    va_end(ap);
    ed.status_rgb = rgb;
    ed.status_t = ed.time;
}

void log_op(const char *who, const Json *req, const Json *res, double ms)
{
    // One JSON object per line: easy to tail, grep and replay.
    Json *e = json_obj();
    char t[32];
    ed_now_iso(t, sizeof(t));
    json_sets(e, "t", t);
    json_sets(e, "who", who ? who : "?");
    json_sets(e, "op", json_gets(req, "op", "?"));
    Json *args = json_get(req, "args");
    if (args) json_set(e, "args", json_clone(args));
    json_setb(e, "ok", json_getb(res, "ok", 0));
    const char *err = json_gets(res, "error", NULL);
    if (err) json_sets(e, "error", err);
    json_setn(e, "ms", (double)(int)(ms * 10) / 10.0);
    char *line = json_write(e, 0);
    json_free(e);
    if (!line) return;
    char path[ED_PATH], dir[ED_PATH];
    ed_path(dir, sizeof(dir), "agent");
    plat_mkdirs(dir);
    ed_path(path, sizeof(path), "agent/log.jsonl");
    if (plat_file_size(path) > 10 * 1024 * 1024) {
        char old[ED_PATH];
        snprintf(old, sizeof(old), "%s.1", path);
        plat_remove_file(old);
        plat_rename_file(path, old);
    }
    FILE *f = fopen(path, "ab");
    if (f) { fputs(line, f); fputc('\n', f); fclose(f); }
    char shown[LEN];
    const char *op = json_gets(req, "op", "?");
    char *a = args ? json_write(args, 0) : NULL;
    snprintf(shown, sizeof(shown), "%-8.8s %s %s%s%s", who ? who : "?", op, a ? a : "",
             err ? "  -> " : "", err ? err : "");
    free(a);
    log_to(CH_AGENT, err ? LOG_ERR : (strcmp(who ? who : "", "human") ? LOG_AGENT : LOG_INFO), shown);
    free(line);
}
