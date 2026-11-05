// Lua scripting (the mx API) and plugins.
//
// Lua runs sandboxed: base, table, string, math and utf8 only (no io/os/package/debug,
// no dofile/loadfile), with an instruction budget so a runaway loop cannot hang the
// editor. Scripts and plugins change the project only through operations, so their
// work is logged and undoable like anyone else's.
//
//   mx.op(name, args)          -> result table      mx.help()   -> list of operations
//   mx.log(...) / print(...)   -> output            mx.scene()  -> active scene id
//   mx.selection()             -> selected object id
//   mx.on(event, fn)           -> fn(op_name, args) after an operation ("*" = any)
//   mx.register_op(name, help, fn) -> a new operation fn(args) -> table
#include "ed.h"
#include "gui.h"
#include "mx_plugin.h"
#include "engine/platform/platform.h"
#include "engine/render/font.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lua_State *L;
static char *s_out;
static size_t s_out_n, s_out_cap;
static char s_who[80] = "lua";
static int s_depth;

static void out(const char *s, size_t n)
{
    if (s_out_n + n + 2 > s_out_cap) {
        size_t cap = s_out_cap ? s_out_cap * 2 : 1024;
        while (cap < s_out_n + n + 2) cap *= 2;
        char *d = (char *)realloc(s_out, cap);
        if (!d) return;
        s_out = d;
        s_out_cap = cap;
    }
    memcpy(s_out + s_out_n, s, n);
    s_out_n += n;
    s_out[s_out_n] = 0;
}

// ---------------------------------------------------------------- Lua <-> JSON
static Json *to_json(lua_State *l, int idx, int depth)
{
    idx = lua_absindex(l, idx);
    switch (lua_type(l, idx)) {
    case LUA_TBOOLEAN: return json_bool(lua_toboolean(l, idx));
    case LUA_TNUMBER: return json_num(lua_tonumber(l, idx));
    case LUA_TSTRING: return json_str(lua_tostring(l, idx));
    case LUA_TTABLE: {
        if (depth > 32) return json_null();
        lua_Integer n = (lua_Integer)lua_rawlen(l, idx);
        int is_arr = n > 0;
        if (is_arr) {
            lua_pushnil(l);
            while (lua_next(l, idx)) {
                if (!lua_isinteger(l, -2)) is_arr = 0;
                lua_pop(l, 1);
                if (!is_arr) { lua_pop(l, 1); break; }
            }
        }
        if (is_arr) {
            Json *a = json_arr();
            for (lua_Integer i = 1; i <= n; i++) {
                lua_rawgeti(l, idx, i);
                json_push(a, to_json(l, -1, depth + 1));
                lua_pop(l, 1);
            }
            return a;
        }
        Json *o = json_obj();
        lua_pushnil(l);
        while (lua_next(l, idx)) {
            if (lua_type(l, -2) == LUA_TSTRING) json_set(o, lua_tostring(l, -2), to_json(l, -1, depth + 1));
            else if (lua_isinteger(l, -2)) {
                char k[32];
                snprintf(k, sizeof(k), "%lld", (long long)lua_tointeger(l, -2));
                json_set(o, k, to_json(l, -1, depth + 1));
            }
            lua_pop(l, 1);
        }
        return o;
    }
    default: return json_null();
    }
}

static void push_json(lua_State *l, const Json *j)
{
    if (!j) { lua_pushnil(l); return; }
    switch (j->type) {
    case JSON_NULL: lua_pushnil(l); break;
    case JSON_BOOL: lua_pushboolean(l, j->num != 0); break;
    case JSON_NUM:
        if (j->num == (lua_Integer)j->num) lua_pushinteger(l, (lua_Integer)j->num);
        else lua_pushnumber(l, j->num);
        break;
    case JSON_STR: lua_pushstring(l, j->str); break;
    case JSON_ARR: {
        lua_createtable(l, json_len(j), 0);
        int i = 1;
        for (const Json *c = j->child; c; c = c->next) { push_json(l, c); lua_rawseti(l, -2, i++); }
        break;
    }
    case JSON_OBJ:
        lua_createtable(l, 0, json_len(j));
        for (const Json *c = j->child; c; c = c->next) { push_json(l, c); lua_setfield(l, -2, c->key ? c->key : ""); }
        break;
    }
}

// ---------------------------------------------------------------- mx API
static int l_print(lua_State *l)
{
    int n = lua_gettop(l);
    for (int i = 1; i <= n; i++) {
        size_t len;
        const char *s = luaL_tolstring(l, i, &len);
        if (i > 1) out("\t", 1);
        out(s, len);
        lua_pop(l, 1);
    }
    out("\n", 1);
    return 0;
}

static int l_op(lua_State *l)
{
    const char *name = luaL_checkstring(l, 1);
    Json *args = lua_gettop(l) >= 2 && lua_istable(l, 2) ? to_json(l, 2, 0) : json_obj();
    if (args->type != JSON_OBJ) { json_free(args); args = json_obj(); }
    Json *r = ops_call(name, args, s_who);
    push_json(l, r);
    json_free(r);
    return 1;
}

static int l_help(lua_State *l)
{
    Json *h = ops_help();
    push_json(l, json_get(h, "ops"));
    json_free(h);
    return 1;
}

static int l_scene(lua_State *l) { lua_pushstring(l, scene_id(cur_scene())); return 1; }
static int l_selection(lua_State *l) { lua_pushstring(l, ed.sel); return 1; }

static int l_json(lua_State *l)
{
    Json *j = to_json(l, 1, 0);
    char *t = json_write(j, (int)luaL_optinteger(l, 2, 0));
    lua_pushstring(l, t ? t : "null");
    free(t);
    json_free(j);
    return 1;
}

static int l_status(lua_State *l)
{
    ed_status(C_COLD, "%s", luaL_checkstring(l, 1));
    return 0;
}

static int l_on(lua_State *l)
{
    const char *ev = luaL_checkstring(l, 1);
    luaL_checktype(l, 2, LUA_TFUNCTION);
    lua_getfield(l, LUA_REGISTRYINDEX, "mx_handlers");
    lua_getfield(l, -1, ev);
    if (lua_isnil(l, -1)) {
        lua_pop(l, 1);
        lua_newtable(l);
        lua_pushvalue(l, -1);
        lua_setfield(l, -3, ev);
    }
    lua_Integer n = (lua_Integer)lua_rawlen(l, -1);
    lua_pushvalue(l, 2);
    lua_rawseti(l, -2, n + 1);
    lua_pop(l, 2);
    return 0;
}

static void hook(lua_State *l, lua_Debug *ar)
{
    (void)ar;
    luaL_error(l, "script ran too long (instruction budget used up)");
}

static int call_protected(int nargs, int nres)
{
    lua_sethook(L, hook, LUA_MASKCOUNT, 50000000);
    int st = lua_pcall(L, nargs, nres, 0);
    lua_sethook(L, NULL, 0, 0);
    return st;
}

// ---------------------------------------------------------------- timed scripts
// A script started with script_start() runs as a coroutine: mx.wait(n) lets n frames
// pass, mx.mouse/mx.key feed input, mx.shot saves the window. Agents use it to drive
// the GUI; the tests use it too.
static lua_State *s_co;
static int s_co_ref = LUA_NOREF;
static int s_wait;
char ed_shot_path[ED_PATH];

static int l_wait(lua_State *l)
{
    lua_Integer n = luaL_optinteger(l, 1, 1);
    lua_pushinteger(l, n < 0 ? 0 : n);
    return lua_yield(l, 1);
}

static int l_mouse(lua_State *l)
{
    int x = (int)luaL_checkinteger(l, 1), y = (int)luaL_checkinteger(l, 2);
    unsigned int buttons = (unsigned int)luaL_optinteger(l, 3, 0);
    unsigned int pressed = (unsigned int)luaL_optinteger(l, 4, 0);
    int wheel = (int)luaL_optinteger(l, 5, 0);
    int k = ed.ui_scale > 0 ? ed.ui_scale : 1;  // scripts speak UI points
    input_inject_mouse(0, 0, wheel, buttons, pressed, x * k, y * k);
    return 0;
}

static int l_key(lua_State *l)
{
    input_test_push_key((int)luaL_checkinteger(l, 1), lua_toboolean(l, 2));
    return 0;
}

static int l_shot(lua_State *l)
{
    snprintf(ed_shot_path, sizeof(ed_shot_path), "%s", luaL_checkstring(l, 1));
    return 0;
}

static int l_quit(lua_State *l) { (void)l; ed.quit = 1; return 0; }

// Presentation settings (what the person sees): never part of the project.
static int l_set_language(lua_State *l)
{
    const char *code = luaL_checkstring(l, 1);
    if (i18n_lang_index(code) < 0) return luaL_error(l, "unknown language %s", code);
    snprintf(ed.language, sizeof(ed.language), "%s", code);
    return 0;
}

static int l_set_font(lua_State *l)
{
    ed.font_pref = !strcmp(luaL_checkstring(l, 1), "system") ? FONT_SYSTEM : FONT_PIXEL;
    return 0;
}

static int l_tr(lua_State *l) { lua_pushstring(l, tr(luaL_checkstring(l, 1))); return 1; }

static int l_type(lua_State *l)
{
    // Typed text goes straight to the focused field.
    extern void gui_type_text(const char *t);
    gui_type_text(luaL_checkstring(l, 1));
    return 0;
}

int script_start(const char *code, const char *chunk)
{
    if (!L && script_init() != 0) return -1;
    if (s_co) { luaL_unref(L, LUA_REGISTRYINDEX, s_co_ref); s_co = NULL; }
    s_co = lua_newthread(L);
    s_co_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (luaL_loadbuffer(s_co, code, strlen(code), chunk) != LUA_OK) {
        ed_log(LOG_ERR, "script: %s", lua_tostring(s_co, -1));
        fprintf(stderr, "script: %s\n", lua_tostring(s_co, -1));
        luaL_unref(L, LUA_REGISTRYINDEX, s_co_ref);
        s_co = NULL;
        return -1;
    }
    s_wait = 0;
    return 0;
}

int script_running(void) { return s_co != NULL; }

void script_tick(void)
{
    if (!s_co) return;
    if (s_wait > 0) { s_wait--; return; }
    snprintf(s_who, sizeof(s_who), "lua");
    int nres = 0;
    int st = lua_resume(s_co, L, 0, &nres);
    if (s_out_n) {
        fputs(s_out, stdout);
        log_to(CH_LUA, LOG_INFO, s_out);
        s_out_n = 0;
        s_out[0] = 0;
    }
    if (st == LUA_YIELD) {
        s_wait = nres > 0 ? (int)lua_tointeger(s_co, -1) : 0;
        if (s_wait > 0) s_wait--;
        lua_settop(s_co, 0);
        return;
    }
    if (st != LUA_OK) {
        const char *e = lua_tostring(s_co, -1);
        ed_log(LOG_ERR, "script: %s", e ? e : "?");
        fprintf(stderr, "script: %s\n", e ? e : "?");
    }
    luaL_unref(L, LUA_REGISTRYINDEX, s_co_ref);
    s_co = NULL;
}

typedef struct { char name[64]; int ref; } LuaOp;
static LuaOp s_lua_ops[64];
static int s_nlua_ops;

static Json *lua_op_trampoline(Json *args, const char *who, void *user)
{
    (void)who;
    LuaOp *op = (LuaOp *)user;
    if (s_depth > 8) { Json *r = json_obj(); json_setb(r, "ok", 0); json_sets(r, "error", "plugin recursion"); return r; }
    s_depth++;
    lua_rawgeti(L, LUA_REGISTRYINDEX, op->ref);
    push_json(L, args);
    Json *r;
    if (call_protected(1, 1) != LUA_OK) {
        r = json_obj();
        json_setb(r, "ok", 0);
        json_sets(r, "error", lua_tostring(L, -1));
    } else {
        r = lua_istable(L, -1) ? to_json(L, -1, 0) : json_obj();
        if (r->type != JSON_OBJ) { json_free(r); r = json_obj(); }
        if (!json_get(r, "ok")) json_setb(r, "ok", 1);
    }
    lua_pop(L, 1);
    s_depth--;
    return r;
}

static int l_register_op(lua_State *l)
{
    const char *name = luaL_checkstring(l, 1);
    const char *help = luaL_optstring(l, 2, "");
    luaL_checktype(l, 3, LUA_TFUNCTION);
    if (s_nlua_ops >= 64) return luaL_error(l, "too many plugin operations");
    LuaOp *op = &s_lua_ops[s_nlua_ops++];
    snprintf(op->name, sizeof(op->name), "%s", name);
    lua_pushvalue(l, 3);
    op->ref = luaL_ref(l, LUA_REGISTRYINDEX);
    ops_register(op->name, help, lua_op_trampoline, op);
    return 0;
}

int script_init(void)
{
    if (L) return 0;
    L = luaL_newstate();
    if (!L) return -1;
    static const luaL_Reg LIBS[] = {{"_G", luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
                                    {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}, {NULL, NULL}};
    for (const luaL_Reg *lib = LIBS; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    lua_pushcfunction(L, l_print);
    lua_setglobal(L, "print");
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "mx_handlers");
    static const luaL_Reg MX[] = {{"op", l_op}, {"help", l_help}, {"log", l_print}, {"scene", l_scene},
                                  {"selection", l_selection}, {"json", l_json}, {"status", l_status},
                                  {"on", l_on}, {"register_op", l_register_op}, {"wait", l_wait},
                                  {"mouse", l_mouse}, {"key", l_key}, {"shot", l_shot}, {"quit", l_quit},
                                  {"type", l_type}, {"set_language", l_set_language}, {"set_font", l_set_font},
                                  {"tr", l_tr}, {NULL, NULL}};
    luaL_newlib(L, MX);
    lua_pushstring(L, ED_VERSION);
    lua_setfield(L, -2, "version");
    lua_setglobal(L, "mx");
    return 0;
}

void script_shutdown(void)
{
    s_co = NULL;
    s_co_ref = LUA_NOREF;
    if (L) lua_close(L);
    L = NULL;
    free(s_out);
    s_out = NULL;
    s_out_n = s_out_cap = 0;
}

static Json *run_chunk(const char *code, const char *chunk, const char *who)
{
    Json *r = json_obj();
    if (!L && script_init() != 0) { json_setb(r, "ok", 0); json_sets(r, "error", "no Lua"); return r; }
    char keep[80];
    snprintf(keep, sizeof(keep), "%s", s_who);
    snprintf(s_who, sizeof(s_who), "%s", who);
    s_out_n = 0;
    if (s_out) s_out[0] = 0;
    // A bare expression ("1 + 2", "mx.scene()") returns its value, like a REPL.
    int st = LUA_ERRSYNTAX;
    size_t rn = strlen(code) + 16;
    char *ret = (char *)malloc(rn);
    if (ret) {
        snprintf(ret, rn, "return %s", code);
        st = luaL_loadbuffer(L, ret, strlen(ret), chunk);
        free(ret);
        if (st != LUA_OK) lua_pop(L, 1);
    }
    if (st != LUA_OK) st = luaL_loadbuffer(L, code, strlen(code), chunk);
    if (st == LUA_OK) st = call_protected(0, LUA_MULTRET);
    int good = st == LUA_OK;
    if (!good) {
        json_sets(r, "error", lua_tostring(L, -1));
        lua_pop(L, 1);
    } else {
        int n = lua_gettop(L);
        for (int i = 1; i <= n; i++) {
            if (lua_istable(L, i)) {
                Json *j = to_json(L, i, 0);
                char *t = json_write(j, 0);
                if (t) { out(t, strlen(t)); out("\n", 1); free(t); }
                json_free(j);
            } else if (!lua_isnil(L, i)) {
                size_t len;
                const char *s = luaL_tolstring(L, i, &len);
                out(s, len);
                out("\n", 1);
                lua_pop(L, 1);
            }
        }
        lua_settop(L, 0);
    }
    json_setb(r, "ok", good);
    json_sets(r, "output", s_out ? s_out : "");
    snprintf(s_who, sizeof(s_who), "%s", keep);
    return r;
}

Json *script_run(const char *code, const char *chunk)
{
    return run_chunk(code, chunk ? chunk : "script", "lua");
}

void script_emit(const char *event, const Json *data)
{
    if (!L || s_depth > 4) return;
    s_depth++;
    const char *keys[2] = {event, "*"};
    for (int k = 0; k < 2; k++) {
        lua_getfield(L, LUA_REGISTRYINDEX, "mx_handlers");
        lua_getfield(L, -1, keys[k]);
        if (lua_istable(L, -1)) {
            lua_Integer n = (lua_Integer)lua_rawlen(L, -1);
            for (lua_Integer i = 1; i <= n; i++) {
                lua_rawgeti(L, -1, i);
                lua_pushstring(L, event);
                push_json(L, data);
                if (call_protected(2, 0) != LUA_OK) {
                    ed_log(LOG_ERR, "mx.on(%s): %s", keys[k], lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            }
        }
        lua_pop(L, 2);
    }
    s_depth--;
}

// ---------------------------------------------------------------- plugins
// Plugin state is a code plus a detail, translated only when drawn: a language switch
// never leaves stale text behind, and plugin.list reports it in English for agents.
enum { PS_LOADED = 0, PS_DISABLED, PS_LOAD_FAILED, PS_INIT_FAILED, PS_MISSING, PS_SCRIPT_ERROR };
static const char *const PS_KEYS[] = {"plugins.loaded", "plugins.disabled", "plugins.load_failed",
                                      "plugins.init_failed", "plugins.missing", "plugins.script_error"};
static const char *const PS_EN[] = {"loaded", "disabled", "cannot load", "init failed", "file missing", "script error"};

typedef struct {
    char name[64], version[32], desc[200], kind[16], detail[160], dir[ED_PATH];
    int ok, enabled, state;
    void *lib;
} Plugin;

static Plugin s_plugins[32];
static int s_nplugins;
static MxApi s_api;
static char *s_api_result;
static const char *s_native_name;

static const char *api_op(const char *request)
{
    char err[200];
    Json *req = json_parse(request, err, sizeof(err));
    Json *r;
    if (!req) {
        r = json_obj();
        json_setb(r, "ok", 0);
        json_sets(r, "error", err);
    } else {
        char who[96];
        snprintf(who, sizeof(who), "plugin:%s", s_native_name ? s_native_name : "?");
        r = ops_exec(req, who);
    }
    free(s_api_result);
    s_api_result = json_write(r, 0);
    json_free(r);
    json_free(req);
    return s_api_result;
}

static void api_log(const char *m) { ed_log(LOG_INFO, "[plugin] %s", m ? m : ""); }

typedef struct { MxOpFn fn; void *user; } NativeOp;
static NativeOp s_native_ops[64];
static int s_nnative;

static Json *native_trampoline(Json *args, const char *who, void *user)
{
    (void)who;
    NativeOp *op = (NativeOp *)user;
    char *a = json_write(args, 0);
    const char *res = op->fn(a ? a : "{}", op->user);
    free(a);
    if (!res) { Json *r = json_obj(); json_setb(r, "ok", 1); return r; }
    char err[200];
    Json *r = json_parse(res, err, sizeof(err));
    if (!r || r->type != JSON_OBJ) {
        json_free(r);
        r = json_obj();
        json_setb(r, "ok", 0);
        json_sets(r, "error", "plugin returned bad JSON");
    } else if (!json_get(r, "ok")) json_setb(r, "ok", 1);
    return r;
}

static int api_register(const char *name, const char *help, MxOpFn fn, void *user)
{
    if (!name || !fn || s_nnative >= 64) return -1;
    NativeOp *op = &s_native_ops[s_nnative++];
    op->fn = fn;
    op->user = user;
    return ops_register(name, help, native_trampoline, op);
}

static int plugin_cb(const char *name, int is_dir, void *user)
{
    (void)user;
    if (!is_dir || name[0] == '.' || s_nplugins >= 32) return 0;
    Plugin *p = &s_plugins[s_nplugins];
    memset(p, 0, sizeof(*p));
    char base[ED_PATH], rel[200];
    snprintf(rel, sizeof(rel), "plugins/%s", name);
    ed_path(base, sizeof(base), rel);
    char path[ED_PATH + 32], err[200];
    snprintf(path, sizeof(path), "%s/plugin.json", base);
    Json *m = json_load(path, err, sizeof(err));
    if (!m) return 0;
    snprintf(p->dir, sizeof(p->dir), "%s", base);
    snprintf(p->name, sizeof(p->name), "%s", json_gets(m, "name", name));
    snprintf(p->version, sizeof(p->version), "%s", json_gets(m, "version", "0"));
    snprintf(p->desc, sizeof(p->desc), "%s", json_gets(m, "description", ""));
    Json *disabled = json_get(json_get(ed.project, "plugins"), "disabled");
    p->enabled = 1;
    for (Json *d = disabled ? disabled->child : NULL; d; d = d->next)
        if (d->type == JSON_STR && !strcmp(d->str, p->name)) p->enabled = 0;
    const char *lua_file = json_gets(m, "lua", NULL);
    Json *native = json_get(m, "native");
#if defined(__APPLE__)
    const char *lib = json_gets(native, "macos", NULL);
#elif defined(_WIN32)
    const char *lib = json_gets(native, "windows", NULL);
#else
    const char *lib = json_gets(native, "linux", NULL);
#endif
    snprintf(p->kind, sizeof(p->kind), "%s%s%s", lua_file ? "Lua" : "", lua_file && lib ? "+" : "", lib ? "C" : "");
    s_nplugins++;
    if (!p->enabled) { p->state = PS_DISABLED; json_free(m); return 0; }
    p->ok = 1;
    if (lib) {
        char lp[ED_PATH + 200];
        snprintf(lp, sizeof(lp), "%s/%s", base, lib);
        p->lib = plat_lib_open(lp);
        MxPluginInitFn init = p->lib ? (MxPluginInitFn)plat_lib_sym(p->lib, "mx_plugin_init") : NULL;
        if (!init) {
            p->state = PS_LOAD_FAILED;
            snprintf(p->detail, sizeof(p->detail), "%s: %s", lib, plat_lib_error());
            p->ok = 0;
        } else {
            s_api.api_version = MX_PLUGIN_API;
            s_api.plugin_name = p->name;
            s_api.op = api_op;
            s_api.log = api_log;
            s_api.register_op = api_register;
            s_native_name = p->name;
            if (init(&s_api) != 0) { p->state = PS_INIT_FAILED; p->ok = 0; }
        }
    }
    if (lua_file && p->ok) {
        char fp[ED_PATH + 200];
        snprintf(fp, sizeof(fp), "%s/%s", base, lua_file);
        int size = 0;
        char *buf = (char *)plat_read_file(fp, &size, 0);
        if (!buf) { p->state = PS_MISSING; snprintf(p->detail, sizeof(p->detail), "%s", lua_file); p->ok = 0; }
        else {
            char *code = (char *)malloc((size_t)size + 1);
            memcpy(code, buf, (size_t)size);
            code[size] = 0;
            free(buf);
            char who[96], chunk[160];
            snprintf(who, sizeof(who), "plugin:%s", p->name);
            snprintf(chunk, sizeof(chunk), "@%s/%s", p->name, lua_file);
            Json *r = run_chunk(code, chunk, who);
            free(code);
            if (!json_getb(r, "ok", 0)) { p->state = PS_SCRIPT_ERROR; snprintf(p->detail, sizeof(p->detail), "%s", json_gets(r, "error", "error")); p->ok = 0; }
            const char *o = json_gets(r, "output", "");
            if (o[0]) ed_log(LOG_INFO, "[%s] %s", p->name, o);
            json_free(r);
        }
    }
    if (p->ok) p->state = PS_LOADED;
    ed_log(p->ok ? LOG_OK : LOG_ERR, "plugin %s: %s%s%s", p->name, PS_EN[p->state], p->detail[0] ? " - " : "", p->detail);
    json_free(m);
    return 0;
}

void plugins_load(void)
{
    // Reload from scratch: a fresh Lua state drops old handlers and operations.
    for (int i = 0; i < s_nplugins; i++)
        if (s_plugins[i].lib) {
            MxPluginShutdownFn sd = (MxPluginShutdownFn)plat_lib_sym(s_plugins[i].lib, "mx_plugin_shutdown");
            if (sd) sd();
            plat_lib_close(s_plugins[i].lib);
        }
    s_nplugins = 0;
    ops_reset_plugins();
    // Keep the Lua state (a running script must survive a reload); drop what plugins
    // registered: their operations and their mx.on handlers.
    if (!L) script_init();
    for (int i = 0; i < s_nlua_ops; i++) luaL_unref(L, LUA_REGISTRYINDEX, s_lua_ops[i].ref);
    s_nlua_ops = 0;
    s_nnative = 0;
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "mx_handlers");
    char dir[ED_PATH];
    ed_path(dir, sizeof(dir), "plugins");
    plat_mkdirs(dir);
    plat_dir_list(dir, plugin_cb, NULL);
}

Json *plugins_list(void)
{
    Json *a = json_arr();
    for (int i = 0; i < s_nplugins; i++) {
        Plugin *p = &s_plugins[i];
        Json *o = json_obj();
        json_sets(o, "name", p->name);
        json_sets(o, "version", p->version);
        json_sets(o, "kind", p->kind);
        json_sets(o, "description", p->desc);
        json_setb(o, "enabled", p->enabled);
        json_setb(o, "loaded", p->ok);
        json_sets(o, "status", PS_EN[p->state]);
        if (p->detail[0]) json_sets(o, "detail", p->detail);
        json_push(a, o);
    }
    return a;
}

static void write_template(void)
{
    char dir[ED_PATH], path[ED_PATH + 32];
    ed_path(dir, sizeof(dir), "plugins/scatter");
    plat_mkdirs(dir);
    snprintf(path, sizeof(path), "%s/plugin.json", dir);
    const char *manifest =
        "{\n  \"name\": \"scatter\",\n  \"version\": \"1.0\",\n"
        "  \"description\": \"Scatters boxes in a ring: an example Lua plugin that adds an operation.\",\n"
        "  \"lua\": \"main.lua\"\n}\n";
    plat_write_file_atomic(path, manifest, (int)strlen(manifest));
    snprintf(path, sizeof(path), "%s/main.lua", dir);
    const char *lua =
        "-- Example plugin: adds the operation \"scatter.ring\".\n"
        "-- Call it from the Lua console, from another plugin or from an agent:\n"
        "--   mexnya agent scatter.ring '{\"count\": 8, \"radius\": 4}'\n"
        "mx.register_op(\"scatter.ring\", \"{count?, radius?, type?} boxes in a ring around the origin\", function(args)\n"
        "  local n = args.count or 6\n"
        "  local r = args.radius or 3\n"
        "  local ids = {}\n"
        "  for i = 1, n do\n"
        "    local a = (i - 1) / n * math.pi * 2\n"
        "    local res = mx.op(\"object.add\", {type = args.type or \"box\", name = \"ring \" .. i,\n"
        "      pos = {math.cos(a) * r, 0.5, math.sin(a) * r}, color = \"#6A5A48\"})\n"
        "    ids[#ids + 1] = res.id\n"
        "  end\n"
        "  return {ok = true, ids = ids}\n"
        "end)\n\n"
        "-- React to edits: tell the person when an agent resolves a note.\n"
        "mx.on(\"note.resolve\", function(op, args)\n"
        "  mx.status(\"note \" .. tostring(args.id) .. \" resolved\")\n"
        "end)\n";
    plat_write_file_atomic(path, lua, (int)strlen(lua));
}

void plugins_page(int x, int y, int w, int h)
{
    gui_rect(x, y, w, h, C_PANEL);
    gui_header(x, y, w, T("tab.plugins"));
    int cy = y + 26;
    if (gui_button("plugins.reload", x + 10, cy, 140, 20)) { plugins_load(); ed_status(C_OK, "%s", T("plugins.reloaded")); }
    if (gui_button("plugins.create_sample", x + 156, cy, 180, 20)) { write_template(); plugins_load(); ed_status(C_OK, T("plugins.created"), "mexnya/plugins/scatter"); }
    font_draw(x + 350, cy + 1, C_DIM, "mexnya/plugins");
    cy += 32;
    for (int i = 0; i < s_nplugins; i++) {
        Plugin *p = &s_plugins[i];
        gui_rect(x + 10, cy, w - 20, 46, C_PANEL2);
        gui_frame(x + 10, cy, w - 20, 46, p->ok ? C_EDGE : (p->enabled ? C_WARN : C_EDGE_IN));
        font_drawf(x + 18, cy + 4, p->enabled ? C_BRIGHT : C_DIM, "%s", p->name);
        font_drawf(x + 18 + font_width(p->name) + 10, cy + 4, C_DIM, "v%s  %s", p->version, p->kind);
        gui_text_clip(x + 18, cy + 24, w - 200, C_TEXT, p->desc);
        char st[256];
        snprintf(st, sizeof(st), "%s%s%s", T(PS_KEYS[p->state]), p->detail[0] ? ": " : "", p->detail);
        gui_text_clip(x + w - 360, cy + 4, 340, p->ok ? C_OK : C_WARN, st);
        if (gui_button_id(gui_id(p->name, 0xE4A), p->enabled ? T("plugins.disable") : T("plugins.enable"), x + w - 80, cy + 22, 60, 18, 0)) {
            Json *pl = json_get(ed.project, "plugins");
            if (!pl) pl = json_set(ed.project, "plugins", json_obj());
            Json *dis = json_get(pl, "disabled");
            if (!dis) dis = json_set(pl, "disabled", json_arr());
            if (p->enabled) json_push(dis, json_str(p->name));
            else
                for (Json *d = dis->child; d; d = d->next)
                    if (d->type == JSON_STR && !strcmp(d->str, p->name)) { json_free(json_detach(d)); break; }
            project_save();
            plugins_load();
            return;
        }
        cy += 52;
    }
    if (!s_nplugins) font_draw(x + 10, cy, C_DIM, T("plugins.none"));
}
