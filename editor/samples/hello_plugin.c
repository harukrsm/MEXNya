// Sample native plugin for the MEXNya Editor: adds "hello.grid", which lays out a grid of
// markers through the editor's own operations. Build: the mx_plugin_hello target, then
// copy the library and plugin.json into mexnya/plugins/hello/.
#include "../mx_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const MxApi *api;
static char result[256];

static int arg_int(const char *json, const char *key, int def)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return def;
    p = strchr(p + strlen(pat), ':');
    return p ? atoi(p + 1) : def;
}

static const char *grid(const char *args, void *user)
{
    (void)user;
    int n = arg_int(args, "size", 3), made = 0;
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    for (int x = 0; x < n; x++)
        for (int z = 0; z < n; z++) {
            char req[256];
            snprintf(req, sizeof(req),
                     "{\"op\": \"object.add\", \"args\": {\"type\": \"marker\", \"name\": \"grid %d-%d\", \"pos\": [%d, 0.3, %d]}}",
                     x, z, x * 2 - n, z * 2 - n);
            if (strstr(api->op(req), "\"ok\":true")) made++;
        }
    snprintf(result, sizeof(result), "{\"ok\": true, \"made\": %d}", made);
    return result;
}

MX_EXPORT int mx_plugin_init(const MxApi *a)
{
    if (a->api_version != MX_PLUGIN_API) return 1;
    api = a;
    api->register_op("hello.grid", "{size?: 1..8} markers in a grid (native sample plugin)", grid, NULL);
    api->log("hello plugin ready");
    return 0;
}

MX_EXPORT void mx_plugin_shutdown(void) { api = NULL; }
