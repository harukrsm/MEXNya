#include "i18n.h"
#include "json.h"
#include "engine/platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const CODES[I18N_LANG_COUNT] = {"en", "zh-Hans", "zh-Hant", "ja", "ko"};
static const char *const NAMES[I18N_LANG_COUNT] = {"English", "简体中文", "繁體中文", "日本語", "한국어"};

typedef struct { char *key, *text; } Entry;
typedef struct { Entry *e; int n, cap; } Table;

#define MAX_DOMAINS 8
static char s_dirs[MAX_DOMAINS][512], s_domains[MAX_DOMAINS][64];
static int s_ndomains;
// Every language, once loaded, stays loaded, and no string is freed before
// i18n_shutdown: a pointer returned by tr() stays valid across any number of switches.
static Table s_tab[I18N_LANG_COUNT];
static int s_loaded[I18N_LANG_COUNT];
static char **s_old;      // texts replaced by a later domain, freed at shutdown
static int s_nold, s_capold;
static int s_lang;  // index into CODES
static int s_rejected;

const char *i18n_lang_code(int i) { return i >= 0 && i < I18N_LANG_COUNT ? CODES[i] : "en"; }
const char *i18n_lang_name(int i) { return i >= 0 && i < I18N_LANG_COUNT ? NAMES[i] : "English"; }
int i18n_lang_index(const char *code)
{
    for (int i = 0; code && i < I18N_LANG_COUNT; i++)
        if (!strcmp(code, CODES[i])) return i;
    return -1;
}
const char *i18n_language(void) { return CODES[s_lang]; }
int i18n_rejected(void) { return s_rejected; }

static char *dupstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static void table_free(Table *t)
{
    for (int i = 0; i < t->n; i++) { free(t->e[i].key); free(t->e[i].text); }
    free(t->e);
    memset(t, 0, sizeof(*t));
}

static int cmp_entry(const void *a, const void *b) { return strcmp(((const Entry *)a)->key, ((const Entry *)b)->key); }

static const Entry *find(const Table *t, const char *key)
{
    int lo = 0, hi = t->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(t->e[mid].key, key);
        if (!c) return &t->e[mid];
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

// The printf conversions of s, e.g. "%s %d %.1f" -> "sdf". -1 on a malformed '%'.
static int conversions(const char *s, char *out, int cap)
{
    int n = 0;
    for (; *s; s++) {
        if (*s != '%') continue;
        s++;
        if (*s == '%') continue;
        while (*s && strchr("-+ #0123456789.*lhzjt", *s)) {
            if (*s == '*' && n < cap - 1) out[n++] = '*';
            s++;
        }
        if (!*s || !strchr("diouxXeEfgGcspa", *s)) return -1;
        char c = *s;
        if (strchr("iouxX", c)) c = 'd';
        if (strchr("eEgGa", c)) c = 'f';
        if (n < cap - 1) out[n++] = c;
    }
    out[n] = 0;
    return n;
}

static void put(Table *t, const char *key, const char *text)
{
    for (int i = 0; i < t->n; i++)
        if (!strcmp(t->e[i].key, key)) {  // later domains may override; keep the old text alive
            if (s_nold == s_capold) {
                int cap = s_capold ? s_capold * 2 : 32;
                char **o = (char **)realloc(s_old, sizeof(char *) * (size_t)cap);
                if (!o) return;
                s_old = o;
                s_capold = cap;
            }
            s_old[s_nold++] = t->e[i].text;
            t->e[i].text = dupstr(text);
            return;
        }
    if (t->n == t->cap) {
        int cap = t->cap ? t->cap * 2 : 256;
        Entry *e = (Entry *)realloc(t->e, sizeof(Entry) * (size_t)cap);
        if (!e) return;
        t->e = e;
        t->cap = cap;
    }
    t->e[t->n].key = dupstr(key);
    t->e[t->n].text = dupstr(text);
    t->n++;
}

static int load_into(Table *t, const char *dir, const char *domain, const char *code, const Table *ref)
{
    char path[700], err[200];
    snprintf(path, sizeof(path), "%s/%s.%s.json", dir, domain, code);
    Json *j = json_load(path, err, sizeof(err));
    if (!j) return 0;
    int n = 0;
    for (Json *c = j->child; c; c = c->next) {
        if (!c->key || c->type != JSON_STR) continue;
        if (ref) {
            // Keep a translation only when its conversions match English exactly.
            const Entry *en = find(ref, c->key);
            char a[32], b[32];
            if (en && (conversions(en->text, a, sizeof(a)) != conversions(c->str, b, sizeof(b)) || strcmp(a, b))) {
                fprintf(stderr, "i18n: %s: \"%s\" changes the format of the English text; ignored\n", path, c->key);
                s_rejected++;
                continue;
            }
            if (!en) continue;  // a key English does not have is unused
        } else {
            char a[32];
            if (conversions(c->str, a, sizeof(a)) < 0) {
                fprintf(stderr, "i18n: %s: \"%s\" has a malformed %% sequence\n", path, c->key);
                continue;
            }
        }
        put(t, c->key, c->str);
        n++;
    }
    json_free(j);
    return n;
}

static void sort(Table *t) { if (t->n) qsort(t->e, (size_t)t->n, sizeof(Entry), cmp_entry); }

static void load_domain(int lang, int d)
{
    load_into(&s_tab[lang], s_dirs[d], s_domains[d], CODES[lang], lang ? &s_tab[0] : NULL);
    sort(&s_tab[lang]);
}

static void ensure_loaded(int lang)
{
    if (!s_loaded[0]) {
        for (int d = 0; d < s_ndomains; d++) load_domain(0, d);
        s_loaded[0] = 1;
    }
    if (lang && !s_loaded[lang]) {
        for (int d = 0; d < s_ndomains; d++) load_domain(lang, d);
        s_loaded[lang] = 1;
    }
}

int i18n_add_domain(const char *dir, const char *domain)
{
    if (!dir || !domain || s_ndomains >= MAX_DOMAINS) return 0;
    snprintf(s_dirs[s_ndomains], sizeof(s_dirs[0]), "%s", dir);
    snprintf(s_domains[s_ndomains], sizeof(s_domains[0]), "%s", domain);
    int d = s_ndomains++;
    int before = s_tab[0].n;
    for (int i = 0; i < I18N_LANG_COUNT; i++)
        if (s_loaded[i] || i == 0 || i == s_lang) { load_domain(i, d); s_loaded[i] = 1; }
    return s_tab[0].n - before;
}

int i18n_set_language(const char *code)
{
    int i = i18n_lang_index(code);
    if (i < 0) i = 0;
    ensure_loaded(i);
    s_lang = i;
    return 0;
}

const char *tr(const char *key)
{
    if (!key) return "";
    const Entry *e = s_lang ? find(&s_tab[s_lang], key) : NULL;
    if (!e) e = find(&s_tab[0], key);
    return e ? e->text : key;
}

const char *i18n_system_language(void)
{
    char buf[64];
    if (plat_preferred_language(buf, sizeof(buf)) != 0) return "en";
    // buf is like "zh-Hans-CN", "zh_TW", "ja", "ko-KR", "en-US".
    if (!strncmp(buf, "zh", 2)) {
        if (strstr(buf, "Hant") || strstr(buf, "TW") || strstr(buf, "HK") || strstr(buf, "MO")) return "zh-Hant";
        return "zh-Hans";
    }
    if (!strncmp(buf, "ja", 2)) return "ja";
    if (!strncmp(buf, "ko", 2)) return "ko";
    return "en";
}

void i18n_shutdown(void)
{
    for (int i = 0; i < I18N_LANG_COUNT; i++) { table_free(&s_tab[i]); s_loaded[i] = 0; }
    for (int i = 0; i < s_nold; i++) free(s_old[i]);
    free(s_old);
    s_old = NULL;
    s_nold = s_capold = 0;
    s_ndomains = 0;
    s_lang = 0;
}
