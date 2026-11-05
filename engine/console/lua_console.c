// Lua 5.4 console integration. Only the safe libraries are opened: no io, os,
// package or debug, therefore no os.execute/io.popen/dynamic libraries.
#include "console.h"
#include "engine/platform/platform.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lua_State *s_L;
static lua_State *s_co;
static int s_co_ref = LUA_NOREF;
static int s_wait_frames;
static int s_lua_ok = 1;

static void report(lua_State *L, int status)
{
    if (status != LUA_OK && status != LUA_YIELD) {
        const char *msg = lua_tostring(L, -1);
        console_print("lua error: %s", msg ? msg : "?");
        lua_pop(L, 1);
        s_lua_ok = 0;
    }
}

static int l_log(lua_State *L)
{
    int n = lua_gettop(L);
    char buf[512];
    size_t off = 0;
    for (int i = 1; i <= n; i++) {
        size_t len = 0;
        const char *s = luaL_tolstring(L, i, &len);
        if (i > 1 && off < sizeof(buf) - 1) buf[off++] = ' ';
        size_t copy = len < sizeof(buf) - off - 1 ? len : sizeof(buf) - off - 1;
        if (copy) memcpy(buf + off, s, copy);
        off += copy;
        lua_pop(L, 1);
    }
    buf[off] = 0;
    console_print_line(buf);
    return 0;
}

static int l_wait(lua_State *L)
{
    lua_Integer n = luaL_optinteger(L, 1, 1);
    if (n < 0) n = 0;
    lua_pushinteger(L, n);
    return lua_yield(L, 1);
}

static int l_screenshot(lua_State *L)
{
    const char *path = luaL_optstring(L, 1, NULL);
    const ConsoleHost *h = console_host();
    if (!h->screenshot) {
        console_print("screenshot: unavailable");
        return 0;
    }
    lua_pushboolean(L, h->screenshot(path) == 0);
    return 1;
}

static int l_cmd(lua_State *L)
{
    const char *line = luaL_checkstring(L, 1);
    lua_pushinteger(L, console_exec_line(line));
    return 1;
}

static int l_run(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    lua_pushinteger(L, console_exec_file(path));
    return 1;
}

static int l_assert(lua_State *L)
{
    if (lua_toboolean(L, 1))
        return 0;
    const char *msg = luaL_optstring(L, 2, "assertion failed");
    return luaL_error(L, "%s", msg);
}

static int l_pos(lua_State *L)
{
    const ConsoleHost *h = console_host();
    float p[3] = {0, 0, 0};
    if (h->pos) h->pos(p);
    lua_newtable(L);
    for (int i = 0; i < 3; i++) {
        lua_pushnumber(L, p[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int l_fps(lua_State *L)
{
    const ConsoleHost *h = console_host();
    float fps = 0, ms = 0;
    if (h->fps) h->fps(&fps, &ms);
    lua_pushnumber(L, fps);
    lua_pushnumber(L, ms);
    return 2;
}

static int l_chunks(lua_State *L)
{
    const ConsoleHost *h = console_host();
    int loaded = 0, missing = 0;
    if (h->chunks) h->chunks(&loaded, &missing);
    lua_pushinteger(L, loaded);
    lua_pushinteger(L, missing);
    return 2;
}

static int l_print(lua_State *L)
{
    (void)l_log(L);
    return 0;
}

static int l_quit(lua_State *L)
{
    (void)L;
    console_request_exit();
    return 0;
}

static const luaL_Reg k_funcs[] = {
    {"log", l_log}, {"wait_frames", l_wait}, {"sleep_frames", l_wait},
    {"screenshot", l_screenshot}, {"run", l_run}, {"cmd", l_cmd}, {"assert", l_assert},
    {"pos", l_pos}, {"fps", l_fps}, {"chunks", l_chunks}, {"quit", l_quit},
    {NULL, NULL}
};

int console_lua_init(void)
{
    s_L = luaL_newstate();
    if (!s_L)
        return -1;
    luaL_requiref(s_L, "_G", luaopen_base, 1);
    lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(s_L, 1);
    // Remove the parts of the base library that read/execute other files.
    lua_pushnil(s_L); lua_setglobal(s_L, "dofile");
    lua_pushnil(s_L); lua_setglobal(s_L, "loadfile");
    lua_pushnil(s_L); lua_setglobal(s_L, "require");
    lua_pushnil(s_L); lua_setglobal(s_L, "print");
    lua_pushglobaltable(s_L);
    luaL_setfuncs(s_L, k_funcs, 0);
    lua_pop(s_L, 1);
    lua_pushcfunction(s_L, l_print);
    lua_setglobal(s_L, "print");
    return 0;
}

void console_lua_shutdown(void)
{
    if (s_co_ref != LUA_NOREF && s_L)
        luaL_unref(s_L, LUA_REGISTRYINDEX, s_co_ref);
    s_co_ref = LUA_NOREF;
    s_co = NULL;
    if (s_L) lua_close(s_L);
    s_L = NULL;
}

int console_lua_busy(void) { return s_co != NULL; }

static int start_co(const char *chunkname, int is_string, const char *path)
{
    if (!s_L)
        return -1;
    if (s_co) {
        console_print("lua: another script is running");
        return 1;
    }
    int st;
    if (is_string)
        st = luaL_loadstring(s_L, path);
    else
        st = luaL_loadfilex(s_L, path, NULL);
    if (st != LUA_OK) {
        report(s_L, st);
        return 1;
    }
    s_co = lua_newthread(s_L);
    s_co_ref = luaL_ref(s_L, LUA_REGISTRYINDEX);
    lua_xmove(s_L, s_co, 1);
    (void)chunkname;
    s_wait_frames = 0;
    s_lua_ok = 1;
    int nres = 0;
    st = lua_resume(s_co, s_L, 0, &nres);
    if (st == LUA_YIELD) {
        s_wait_frames = (int)lua_tointeger(s_co, -1);
        lua_pop(s_co, 1);
        return 0;
    }
    if (st == LUA_OK) {
        luaL_unref(s_L, LUA_REGISTRYINDEX, s_co_ref);
        s_co_ref = LUA_NOREF;
        s_co = NULL;
        return s_lua_ok ? 0 : 1;
    }
    report(s_co, st);
    luaL_unref(s_L, LUA_REGISTRYINDEX, s_co_ref);
    s_co_ref = LUA_NOREF;
    s_co = NULL;
    return 1;
}

int console_lua_run_file(const char *path) { return start_co(path, 0, path); }
int console_lua_run_string(const char *code, const char *chunkname) { return start_co(chunkname, 1, code); }

void console_lua_update(void)
{
    if (!s_co)
        return;
    if (s_wait_frames > 0) {
        s_wait_frames--;
        return;
    }
    int nres = 0;
    int st = lua_resume(s_co, s_L, 0, &nres);
    if (st == LUA_YIELD) {
        s_wait_frames = (int)lua_tointeger(s_co, -1);
        lua_pop(s_co, 1);
        return;
    }
    if (st == LUA_OK) {
        luaL_unref(s_L, LUA_REGISTRYINDEX, s_co_ref);
        s_co_ref = LUA_NOREF;
        s_co = NULL;
        if (!s_lua_ok)
            console_set_exit_code(1);
        return;
    }
    report(s_co, st);
    luaL_unref(s_L, LUA_REGISTRYINDEX, s_co_ref);
    s_co_ref = LUA_NOREF;
    s_co = NULL;
    console_set_exit_code(1);
}

static int cmd_lua(int argc, char **argv)
{
    if (argc < 2) {
        console_print("usage: lua <file.lua>");
        return 2;
    }
    // A path ending in .lua runs as a file; anything else is source text.
    size_t n = strlen(argv[1]);
    if (n > 4 && strcmp(argv[1] + n - 4, ".lua") == 0)
        return console_lua_run_file(argv[1]);
    return console_lua_run_string(argv[1], "=console");
}

void console_lua_register_command(void)
{
    console_register("lua", "<file.lua|code>", "run a Lua script", cmd_lua);
}