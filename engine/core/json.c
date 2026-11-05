#include "json.h"
#include "engine/platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static Json *node(JsonType t)
{
    Json *j = (Json *)calloc(1, sizeof(Json));
    if (j) j->type = t;
    return j;
}

Json *json_null(void) { return node(JSON_NULL); }
Json *json_bool(int b) { Json *j = node(JSON_BOOL); if (j) j->num = b ? 1 : 0; return j; }
Json *json_num(double n) { Json *j = node(JSON_NUM); if (j) j->num = n; return j; }
Json *json_str(const char *s) { Json *j = node(JSON_STR); if (j) j->str = dup(s ? s : ""); return j; }
Json *json_arr(void) { return node(JSON_ARR); }
Json *json_obj(void) { return node(JSON_OBJ); }

void json_free(Json *j)
{
    if (!j) return;
    Json *c = j->child;
    while (c) {
        Json *next = c->next;
        c->next = NULL;
        json_free(c);
        c = next;
    }
    free(j->key);
    free(j->str);
    free(j);
}

// ---------------------------------------------------------------- parse
typedef struct {
    const char *p;
    int line;
    char *err;
    int err_size;
    int failed;
} P;

static void fail(P *p, const char *msg)
{
    if (!p->failed && p->err && p->err_size > 0)
        snprintf(p->err, (size_t)p->err_size, "line %d: %s", p->line, msg);
    p->failed = 1;
}

static void ws(P *p)
{
    for (;;) {
        char c = *p->p;
        if (c == '\n') { p->line++; p->p++; }
        else if (c == ' ' || c == '\t' || c == '\r') p->p++;
        else if (c == '/' && p->p[1] == '/') { while (*p->p && *p->p != '\n') p->p++; }  // lenient
        else break;
    }
}

static void put_utf8(char **o, unsigned int cp)
{
    char *d = *o;
    if (cp < 0x80) *d++ = (char)cp;
    else if (cp < 0x800) { *d++ = (char)(0xC0 | cp >> 6); *d++ = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        *d++ = (char)(0xE0 | cp >> 12); *d++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *d++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *d++ = (char)(0xF0 | cp >> 18); *d++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *d++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *d++ = (char)(0x80 | (cp & 0x3F));
    }
    *o = d;
}

static int hex4(const char *s, unsigned int *v)
{
    *v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        *v <<= 4;
        if (c >= '0' && c <= '9') *v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') *v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') *v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    return 1;
}

static char *pstring(P *p)
{
    if (*p->p != '"') { fail(p, "expected a string"); return NULL; }
    const char *s = ++p->p;
    size_t cap = 0;
    while (s[cap] && s[cap] != '"') { if (s[cap] == '\\' && s[cap + 1]) cap++; cap++; }
    char *out = (char *)malloc(cap + 4), *o = out;
    if (!out) { fail(p, "out of memory"); return NULL; }
    while (*p->p && *p->p != '"') {
        char c = *p->p++;
        if (c == '\n') p->line++;
        if (c != '\\') { *o++ = c; continue; }
        c = *p->p++;
        switch (c) {
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'u': {
            unsigned int cp;
            if (!hex4(p->p, &cp)) { fail(p, "bad \\u escape"); free(out); return NULL; }
            p->p += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && p->p[0] == '\\' && p->p[1] == 'u') {
                unsigned int lo;
                if (hex4(p->p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p->p += 6;
                }
            }
            put_utf8(&o, cp);
            break;
        }
        case 0: fail(p, "unterminated string"); free(out); return NULL;
        default: *o++ = c; break;
        }
    }
    if (*p->p != '"') { fail(p, "unterminated string"); free(out); return NULL; }
    p->p++;
    *o = 0;
    return out;
}

static Json *pvalue(P *p, int depth);

static Json *pvalue(P *p, int depth)
{
    if (depth > 64) { fail(p, "nested too deeply"); return NULL; }
    ws(p);
    char c = *p->p;
    if (c == '{' || c == '[') {
        int obj = c == '{';
        Json *j = node(obj ? JSON_OBJ : JSON_ARR), *last = NULL;
        p->p++;
        ws(p);
        if (*p->p == (obj ? '}' : ']')) { p->p++; return j; }
        for (;;) {
            ws(p);
            char *key = NULL;
            if (obj) {
                key = pstring(p);
                if (!key) { json_free(j); return NULL; }
                ws(p);
                if (*p->p != ':') { fail(p, "expected ':'"); free(key); json_free(j); return NULL; }
                p->p++;
            }
            Json *v = pvalue(p, depth + 1);
            if (!v) { free(key); json_free(j); return NULL; }
            v->key = key;
            v->parent = j;
            if (last) last->next = v; else j->child = v;
            last = v;
            ws(p);
            if (*p->p == ',') { p->p++; ws(p); if (*p->p == (obj ? '}' : ']')) { p->p++; return j; } continue; }
            if (*p->p == (obj ? '}' : ']')) { p->p++; return j; }
            fail(p, obj ? "expected ',' or '}'" : "expected ',' or ']'");
            json_free(j);
            return NULL;
        }
    }
    if (c == '"') {
        char *s = pstring(p);
        if (!s) return NULL;
        Json *j = node(JSON_STR);
        j->str = s;
        return j;
    }
    if (!strncmp(p->p, "true", 4)) { p->p += 4; return json_bool(1); }
    if (!strncmp(p->p, "false", 5)) { p->p += 5; return json_bool(0); }
    if (!strncmp(p->p, "null", 4)) { p->p += 4; return json_null(); }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char *end;
        double v = strtod(p->p, &end);
        if (end == p->p) { fail(p, "bad number"); return NULL; }
        p->p = end;
        return json_num(v);
    }
    fail(p, c ? "unexpected character" : "unexpected end of text");
    return NULL;
}

Json *json_parse(const char *text, char *err, int err_size)
{
    if (err && err_size > 0) err[0] = 0;
    if (!text) return NULL;
    P p = {text, 1, err, err_size, 0};
    if ((unsigned char)p.p[0] == 0xEF && (unsigned char)p.p[1] == 0xBB && (unsigned char)p.p[2] == 0xBF) p.p += 3;
    Json *j = pvalue(&p, 0);
    if (!j) return NULL;
    ws(&p);
    if (*p.p) { fail(&p, "text after the value"); json_free(j); return NULL; }
    return j;
}

Json *json_load(const char *path, char *err, int err_size)
{
    int size = 0;
    char *buf = (char *)plat_read_file(path, &size, 0);
    if (!buf) {
        if (err && err_size > 0) snprintf(err, (size_t)err_size, "cannot read %s", path);
        return NULL;
    }
    char *text = (char *)malloc((size_t)size + 1);
    if (!text) { free(buf); return NULL; }
    memcpy(text, buf, (size_t)size);
    text[size] = 0;
    free(buf);
    Json *j = json_parse(text, err, err_size);
    free(text);
    return j;
}

// ---------------------------------------------------------------- write
typedef struct { char *d; size_t n, cap; } B;

static void bput(B *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 1024;
        while (cap < b->n + n + 1) cap *= 2;
        char *d = (char *)realloc(b->d, cap);
        if (!d) return;
        b->d = d;
        b->cap = cap;
    }
    memcpy(b->d + b->n, s, n);
    b->n += n;
    b->d[b->n] = 0;
}
static void bs(B *b, const char *s) { bput(b, s, strlen(s)); }

static void wstr(B *b, const char *s)
{
    bs(b, "\"");
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        char e[8];
        if (c == '"') bs(b, "\\\"");
        else if (c == '\\') bs(b, "\\\\");
        else if (c == '\n') bs(b, "\\n");
        else if (c == '\t') bs(b, "\\t");
        else if (c == '\r') bs(b, "\\r");
        else if (c < 0x20) { snprintf(e, sizeof(e), "\\u%04x", c); bs(b, e); }
        else bput(b, (const char *)&c, 1);
    }
    bs(b, "\"");
}

static int is_flat(const Json *j)
{
    // Short arrays of numbers stay on one line: [1, 2, 3].
    if (j->type != JSON_ARR) return 0;
    int n = 0;
    for (const Json *c = j->child; c; c = c->next, n++)
        if (c->type == JSON_ARR || c->type == JSON_OBJ || n > 16) return 0;
    return 1;
}

static void wval(B *b, const Json *j, int indent, int depth)
{
    char tmp[64];
    switch (j->type) {
    case JSON_NULL: bs(b, "null"); break;
    case JSON_BOOL: bs(b, j->num ? "true" : "false"); break;
    case JSON_NUM:
        if (isfinite(j->num) && j->num == floor(j->num) && fabs(j->num) < 1e15)
            snprintf(tmp, sizeof(tmp), "%.0f", j->num);
        else if (isfinite(j->num))
            snprintf(tmp, sizeof(tmp), "%.6g", j->num);
        else
            snprintf(tmp, sizeof(tmp), "0");
        bs(b, tmp);
        break;
    case JSON_STR: wstr(b, j->str); break;
    case JSON_ARR:
    case JSON_OBJ: {
        int obj = j->type == JSON_OBJ;
        if (!j->child) { bs(b, obj ? "{}" : "[]"); break; }
        int flat = indent == 0 || is_flat(j);
        bs(b, obj ? "{" : "[");
        for (const Json *c = j->child; c; c = c->next) {
            if (!flat) {
                bs(b, "\n");
                for (int i = 0; i < (depth + 1) * indent; i++) bs(b, " ");
            }
            if (obj) { wstr(b, c->key ? c->key : ""); bs(b, indent ? ": " : ":"); }
            wval(b, c, indent, depth + 1);
            if (c->next) bs(b, flat && indent ? ", " : ",");
        }
        if (!flat) {
            bs(b, "\n");
            for (int i = 0; i < depth * indent; i++) bs(b, " ");
        }
        bs(b, obj ? "}" : "]");
        break;
    }
    }
}

char *json_write(const Json *j, int indent)
{
    B b = {0};
    if (j) wval(&b, j, indent, 0);
    else bs(&b, "null");
    if (indent) bs(&b, "\n");
    return b.d;
}

int json_save(const char *path, const Json *j)
{
    char *t = json_write(j, 2);
    if (!t) return -1;
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int r = plat_write_file(tmp, t, (int)strlen(t));
    free(t);
    if (r != 0) return -1;
    plat_remove_file(path);
    return plat_rename_file(tmp, path);
}

// ---------------------------------------------------------------- access
int json_len(const Json *j)
{
    int n = 0;
    if (j && (j->type == JSON_ARR || j->type == JSON_OBJ))
        for (const Json *c = j->child; c; c = c->next) n++;
    return n;
}

Json *json_at(const Json *arr, int i)
{
    if (!arr || i < 0) return NULL;
    Json *c = arr->child;
    while (c && i-- > 0) c = c->next;
    return c;
}

Json *json_get(const Json *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJ || !key) return NULL;
    for (Json *c = obj->child; c; c = c->next)
        if (c->key && !strcmp(c->key, key)) return c;
    return NULL;
}

int json_index_of(const Json *parent, const Json *child)
{
    int i = 0;
    for (const Json *c = parent ? parent->child : NULL; c; c = c->next, i++)
        if (c == child) return i;
    return -1;
}

const char *json_gets(const Json *obj, const char *key, const char *def)
{
    Json *v = json_get(obj, key);
    return v && v->type == JSON_STR ? v->str : def;
}

double json_getn(const Json *obj, const char *key, double def)
{
    Json *v = json_get(obj, key);
    return v && (v->type == JSON_NUM || v->type == JSON_BOOL) ? v->num : def;
}

int json_getb(const Json *obj, const char *key, int def)
{
    Json *v = json_get(obj, key);
    return v && (v->type == JSON_BOOL || v->type == JSON_NUM) ? v->num != 0 : def;
}

int json_getv(const Json *obj, const char *key, float *out, int n)
{
    Json *v = json_get(obj, key);
    int k = 0;
    if (!v || v->type != JSON_ARR) return 0;
    for (Json *c = v->child; c && k < n; c = c->next)
        if (c->type == JSON_NUM) out[k++] = (float)c->num;
    return k;
}

// ---------------------------------------------------------------- edit
Json *json_detach(Json *child)
{
    if (!child || !child->parent) return child;
    Json *p = child->parent;
    if (p->child == child) p->child = child->next;
    else {
        Json *c = p->child;
        while (c && c->next != child) c = c->next;
        if (c) c->next = child->next;
    }
    child->next = NULL;
    child->parent = NULL;
    return child;
}

Json *json_insert(Json *arr, int index, Json *val)
{
    if (!arr || !val) { json_free(val); return NULL; }
    if (val->parent) json_detach(val);
    val->parent = arr;
    if (index == 0 || !arr->child) {
        val->next = arr->child;
        arr->child = val;
        return val;
    }
    Json *c = arr->child;
    for (int i = 1; c->next && (index < 0 || i < index); i++) c = c->next;
    val->next = c->next;
    c->next = val;
    return val;
}

Json *json_push(Json *arr, Json *val) { return json_insert(arr, -1, val); }

Json *json_set(Json *obj, const char *key, Json *val)
{
    if (!obj || obj->type != JSON_OBJ || !key || !val) { json_free(val); return NULL; }
    char *k = dup(key);
    free(val->key);
    val->key = k;
    Json *old = json_get(obj, k);
    if (!old) return json_push(obj, val);
    // Replace in place so the member keeps its position.
    val->parent = obj;
    val->next = old->next;
    if (obj->child == old) obj->child = val;
    else {
        Json *c = obj->child;
        while (c->next != old) c = c->next;
        c->next = val;
    }
    old->next = NULL;
    old->parent = NULL;
    json_free(old);
    return val;
}

Json *json_sets(Json *obj, const char *key, const char *s) { return json_set(obj, key, json_str(s)); }
Json *json_setn(Json *obj, const char *key, double n) { return json_set(obj, key, json_num(n)); }
Json *json_setb(Json *obj, const char *key, int b) { return json_set(obj, key, json_bool(b)); }
Json *json_setv(Json *obj, const char *key, const float *v, int n)
{
    Json *a = json_arr();
    for (int i = 0; i < n; i++) json_push(a, json_num(floor(v[i] * 10000.0 + 0.5) / 10000.0));
    return json_set(obj, key, a);
}

void json_remove(Json *obj, const char *key)
{
    Json *v = json_get(obj, key);
    if (v) json_free(json_detach(v));
}

Json *json_find_id(const Json *arr, const char *id)
{
    if (!arr || !id) return NULL;
    for (Json *c = arr->child; c; c = c->next) {
        const char *cid = json_gets(c, "id", NULL);
        if (cid && !strcmp(cid, id)) return c;
    }
    return NULL;
}

Json *json_path(const Json *root, const char *path)
{
    Json *cur = (Json *)root;
    char part[128];
    while (cur && path && *path) {
        int n = 0;
        while (path[n] && path[n] != '.' && n < (int)sizeof(part) - 1) { part[n] = path[n]; n++; }
        part[n] = 0;
        path += n;
        if (*path == '.') path++;
        if (part[0] == '#') cur = json_find_id(cur, part + 1);
        else if (cur->type == JSON_ARR) {
            char *end;
            long i = strtol(part, &end, 10);
            cur = *end ? NULL : json_at(cur, (int)i);
        } else cur = json_get(cur, part);
    }
    return cur;
}

Json *json_clone(const Json *j)
{
    if (!j) return NULL;
    Json *c = node(j->type);
    if (!c) return NULL;
    c->num = j->num;
    c->str = dup(j->str);
    c->key = dup(j->key);
    for (const Json *k = j->child; k; k = k->next) {
        Json *kc = json_clone(k);
        free(kc->key);
        kc->key = dup(k->key);
        json_push(c, kc);
    }
    return c;
}

int json_equal(const Json *a, const Json *b)
{
    if (!a || !b) return a == b;
    if (a->type != b->type) return 0;
    switch (a->type) {
    case JSON_NULL: return 1;
    case JSON_BOOL:
    case JSON_NUM: return a->num == b->num;
    case JSON_STR: return !strcmp(a->str, b->str);
    default: break;
    }
    const Json *x = a->child, *y = b->child;
    for (; x && y; x = x->next, y = y->next) {
        if (a->type == JSON_OBJ && strcmp(x->key ? x->key : "", y->key ? y->key : "")) return 0;
        if (!json_equal(x, y)) return 0;
    }
    return !x && !y;
}
