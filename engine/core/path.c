#include "engine/core/path.h"

#include "engine/core/mem.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int is_sep(char c) { return c == '/' || c == '\\'; }

static size_t drive_len(const char *p)
{
    return isalpha((unsigned char)p[0]) && p[1] == ':' ? 2 : 0;
}

int mx_path_is_absolute(const char *path)
{
    if (!path || !path[0]) return 0;
    if (is_sep(path[0])) return 1;
    return isalpha((unsigned char)path[0]) && path[1] == ':';
}

void mx_path_normalize(char *path)
{
    if (!path) return;
    size_t len = strlen(path);
    char *in = (char *)mx_mem_alloc(len + 1);
    char *out = (char *)mx_mem_alloc(len + 2);
    if (!in || !out) {
        mx_mem_free(in);
        mx_mem_free(out);
        return;
    }
    memcpy(in, path, len + 1);
    for (size_t i = 0; i < len; i++)
        if (in[i] == '\\') in[i] = '/';
    size_t dlen = drive_len(in);
    int absolute = in[dlen] == '/';
    size_t o = 0;
    if (dlen) {
        out[o++] = in[0];
        out[o++] = ':';
    }
    if (absolute) out[o++] = '/';
    size_t prefix = o;
    size_t i = dlen;
    while (i < len) {
        while (i < len && in[i] == '/') i++;
        size_t start = i;
        while (i < len && in[i] != '/') i++;
        size_t n = i - start;
        if (n == 0) break;
        if (n == 1 && in[start] == '.') continue;
        if (n == 2 && in[start] == '.' && in[start + 1] == '.') {
            if (o > prefix) {
                while (o > prefix && out[o - 1] != '/') o--;
                if (o > prefix) o--;
            } else if (!absolute) {
                if (o > prefix) out[o++] = '/';
                out[o++] = '.';
                out[o++] = '.';
            }
            continue;
        }
        if (o > 0 && out[o - 1] != '/') out[o++] = '/';
        memcpy(out + o, in + start, n);
        o += n;
    }
    if (o == prefix && !absolute && dlen == 0) {
        out[o++] = '.';
    } else if (o == prefix && absolute && o == 0) {
        out[o++] = '/';
    }
    out[o] = 0;
    memcpy(path, out, o + 1);
    mx_mem_free(in);
    mx_mem_free(out);
}

void mx_path_join(char *out, size_t out_size, const char *a, const char *b)
{
    if (!out || out_size == 0) return;
    if (!a) a = "";
    if (!b) b = "";
    if (mx_path_is_absolute(b) || !a[0]) {
        snprintf(out, out_size, "%s", b);
        return;
    }
    size_t n = strlen(a);
    int sep = n > 0 && !is_sep(a[n - 1]);
    snprintf(out, out_size, "%s%s%s", a, sep ? "/" : "", b);
}

static int is_sub(const char *root, const char *path)
{
    size_t rn = strlen(root);
    if (rn == 0) return 0;
    if (strcmp(root, path) == 0) return 1;
    if (strncmp(root, path, rn) != 0) return 0;
    if (root[rn - 1] == '/') return 1;
    return path[rn] == '/';
}

int mx_path_is_subpath(const char *root, const char *path)
{
    char *r, *p;
    size_t rl, pl;
    int result;
    if (!root || !path) return 0;
    rl = strlen(root);
    pl = strlen(path);
    r = (char *)mx_mem_alloc(rl + 2);
    p = (char *)mx_mem_alloc(pl + 2);
    if (!r || !p) {
        mx_mem_free(r);
        mx_mem_free(p);
        return 0;
    }
    memcpy(r, root, rl + 1);
    memcpy(p, path, pl + 1);
    mx_path_normalize(r);
    mx_path_normalize(p);
    result = !(r[0] == '.' && r[1] == 0) && is_sub(r, p);
    mx_mem_free(r);
    mx_mem_free(p);
    return result;
}

const char *mx_path_ext(const char *path)
{
    if (!path) return "";
    const char *base = path, *dot = NULL;
    for (const char *c = path; *c; c++) {
        if (is_sep(*c)) {
            base = c + 1;
            dot = NULL;
        } else if (*c == '.') {
            dot = c;
        }
    }
    if (!dot || dot == base) return "";
    return dot + 1;
}
