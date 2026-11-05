// MEXNya Editor native plugin ABI (version 1).
//
// A native plugin is a shared library (.dylib / .so / .dll) next to its plugin.json:
//
//   mexnya/plugins/<name>/plugin.json
//     {"name": "...", "version": "1.0", "description": "...",
//      "native": {"macos": "libname.dylib", "linux": "libname.so", "windows": "name.dll"}}
//
// It exports mx_plugin_init (and optionally mx_plugin_shutdown). Everything crosses the
// boundary as JSON text, so the ABI stays this small and any language with a C FFI works.
// Plain C, no engine headers needed.
#ifndef MX_PLUGIN_H
#define MX_PLUGIN_H

#define MX_PLUGIN_API 1

#ifdef _WIN32
#define MX_EXPORT __declspec(dllexport)
#else
#define MX_EXPORT __attribute__((visibility("default")))
#endif

// An operation handler: args as JSON text in, result as JSON text out. The returned
// text belongs to the plugin and must stay valid until the next call of this handler.
// Return NULL for {"ok": true}.
typedef const char *(*MxOpFn)(const char *args_json, void *user);

typedef struct MxApi {
    int api_version;            // MX_PLUGIN_API
    const char *plugin_name;
    // Runs any editor operation: request = {"op": "...", "args": {...}}. The result text
    // is valid until the next call of op(). Logged as "plugin:<name>".
    const char *(*op)(const char *request_json);
    void (*log)(const char *message);
    // Adds an operation, e.g. "terrain.raise"; agents and Lua can call it like any other.
    int (*register_op)(const char *name, const char *help, MxOpFn fn, void *user);
} MxApi;

// Return 0 on success.
typedef int (*MxPluginInitFn)(const MxApi *api);
typedef void (*MxPluginShutdownFn)(void);

#endif
