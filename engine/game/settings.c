#include "engine/game/settings.h"

#include <string.h>

static void merge_defaults(Json *target, const Json *defaults)
{
    if (!target || !defaults || defaults->type != JSON_OBJ) return;
    for (const Json *d = defaults->child; d; d = d->next) {
        if (!d->key || !d->key[0]) continue;
        Json *have = json_get(target, d->key);
        if (!have) {
            json_set(target, d->key, json_clone(d));
        } else if (have->type == JSON_OBJ && d->type == JSON_OBJ) {
            merge_defaults(have, d);
        }
    }
}

Json *mx_settings_load(const char *path, const Json *defaults)
{
    Json *doc = path ? json_load(path, NULL, 0) : NULL;
    if (!doc) doc = json_obj();
    if (doc->type != JSON_OBJ) {
        json_free(doc);
        doc = json_obj();
    }
    merge_defaults(doc, defaults);
    if (!json_get(doc, "schema_version")) json_setn(doc, "schema_version", MX_SETTINGS_VERSION);
    return doc;
}

int mx_settings_save(const char *path, const Json *settings)
{
    if (!path || !settings || settings->type != JSON_OBJ) return -1;
    Json *copy = json_clone(settings);
    if (!copy) return -1;
    json_setn(copy, "schema_version", MX_SETTINGS_VERSION);
    int r = json_save(path, copy);
    json_free(copy);
    return r;
}
