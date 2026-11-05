// A small JSON DOM: parse, edit, write. Objects keep their key order, so files written
// by tools diff cleanly and stay readable for people and for AI agents.
#ifndef MC_CORE_JSON_H
#define MC_CORE_JSON_H

typedef enum { JSON_NULL = 0, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ } JsonType;

typedef struct Json {
    JsonType type;
    char *key;            // member name inside an object, else NULL
    double num;           // JSON_NUM, JSON_BOOL (0/1)
    char *str;            // JSON_STR
    struct Json *child;   // first element/member
    struct Json *next;
    struct Json *parent;
} Json;

// Returns NULL on a syntax error; err (optional) gets "line N: message".
Json *json_parse(const char *text, char *err, int err_size);
Json *json_load(const char *path, char *err, int err_size);
// malloc'd text. indent 0 = one line, otherwise spaces per level.
char *json_write(const Json *j, int indent);
int json_save(const char *path, const Json *j);
void json_free(Json *j);
Json *json_clone(const Json *j);
int json_equal(const Json *a, const Json *b);

Json *json_null(void);
Json *json_bool(int b);
Json *json_num(double n);
Json *json_str(const char *s);
Json *json_arr(void);
Json *json_obj(void);

int json_len(const Json *j);
Json *json_at(const Json *arr, int i);
Json *json_get(const Json *obj, const char *key);
int json_index_of(const Json *parent, const Json *child);
// Typed reads with defaults (missing or wrong type returns the default).
const char *json_gets(const Json *obj, const char *key, const char *def);
double json_getn(const Json *obj, const char *key, double def);
int json_getb(const Json *obj, const char *key, int def);
// Arrays of numbers: reads up to n values into out, returns how many were read.
int json_getv(const Json *obj, const char *key, float *out, int n);

// Edits take ownership of val. set replaces an existing member in place.
Json *json_set(Json *obj, const char *key, Json *val);
Json *json_sets(Json *obj, const char *key, const char *s);
Json *json_setn(Json *obj, const char *key, double n);
Json *json_setb(Json *obj, const char *key, int b);
Json *json_setv(Json *obj, const char *key, const float *v, int n);
Json *json_push(Json *arr, Json *val);
Json *json_insert(Json *arr, int index, Json *val);
// Unlinks child from its parent (caller owns it afterwards).
Json *json_detach(Json *child);
void json_remove(Json *obj, const char *key);
// The element of arr whose "id" member equals id.
Json *json_find_id(const Json *arr, const char *id);
// Dotted path: "scenes.2.objects" or "objects.#crate" (# = by id).
Json *json_path(const Json *root, const char *path);

#endif
