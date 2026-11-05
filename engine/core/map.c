#include "engine/core/map.h"

#include "engine/core/mem.h"

#include <string.h>

enum { MAP_EMPTY = 0, MAP_FULL = 1, MAP_TOMB = 2 };

struct MxMap {
    uint64_t *keys;
    void **values;
    unsigned char *state;
    int cap, count, tombs;
};

uint64_t mx_hash_bytes(const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t mx_hash_str(const char *s)
{
    if (!s) return 0;
    return mx_hash_bytes(s, strlen(s));
}

static int map_slot(const uint64_t *keys, const unsigned char *state, int cap, uint64_t key, int *found)
{
    uint64_t mixed = key;
    mixed ^= mixed >> 30;
    mixed *= 0xbf58476d1ce4e5b9ull;
    mixed ^= mixed >> 27;
    mixed *= 0x94d049bb133111ebull;
    mixed ^= mixed >> 31;
    int mask = cap - 1;
    int at = (int)(mixed & (uint64_t)mask);
    int first_tomb = -1;
    for (int i = 0; i < cap; i++) {
        int s = state[at];
        if (s == MAP_EMPTY) {
            *found = 0;
            return first_tomb >= 0 ? first_tomb : at;
        }
        if (s == MAP_TOMB) {
            if (first_tomb < 0) first_tomb = at;
        } else if (keys[at] == key) {
            *found = 1;
            return at;
        }
        at = (at + 1) & mask;
    }
    *found = 0;
    return first_tomb;
}

static int map_resize(MxMap *m, int cap)
{
    uint64_t *keys = (uint64_t *)mx_mem_alloc(sizeof(uint64_t) * (size_t)cap);
    void **values = (void **)mx_mem_alloc(sizeof(void *) * (size_t)cap);
    unsigned char *state = (unsigned char *)mx_mem_calloc((size_t)cap, 1);
    if (!keys || !values || !state) {
        mx_mem_free(keys);
        mx_mem_free(values);
        mx_mem_free(state);
        return 0;
    }
    if (m->cap) {
        for (int i = 0; i < m->cap; i++) {
            if (m->state[i] != MAP_FULL) continue;
            int found = 0;
            int at = map_slot(keys, state, cap, m->keys[i], &found);
            keys[at] = m->keys[i];
            values[at] = m->values[i];
            state[at] = MAP_FULL;
        }
    }
    mx_mem_free(m->keys);
    mx_mem_free(m->values);
    mx_mem_free(m->state);
    m->keys = keys;
    m->values = values;
    m->state = state;
    m->cap = cap;
    m->tombs = 0;
    return 1;
}

MxMap *mx_map_new(void)
{
    MxMap *m = (MxMap *)mx_mem_calloc(1, sizeof(*m));
    if (!m) return NULL;
    if (!map_resize(m, 16)) {
        mx_mem_free(m);
        return NULL;
    }
    return m;
}

void mx_map_free(MxMap *m)
{
    if (!m) return;
    mx_mem_free(m->keys);
    mx_mem_free(m->values);
    mx_mem_free(m->state);
    mx_mem_free(m);
}

void mx_map_clear(MxMap *m)
{
    if (!m) return;
    memset(m->state, 0, (size_t)m->cap);
    m->count = 0;
    m->tombs = 0;
}

void *mx_map_get(const MxMap *m, uint64_t key)
{
    if (!m || !m->cap) return NULL;
    int found = 0;
    int at = map_slot(m->keys, m->state, m->cap, key, &found);
    return found ? m->values[at] : NULL;
}

int mx_map_put(MxMap *m, uint64_t key, void *value)
{
    if (!m) return 0;
    if ((m->count + m->tombs + 1) * 10 >= m->cap * 7) {
        int cap = m->cap * 2;
        if (!cap) cap = 16;
        if (!map_resize(m, cap)) return 0;
    }
    int found = 0;
    int at = map_slot(m->keys, m->state, m->cap, key, &found);
    if (at < 0) return 0;
    if (found) {
        m->values[at] = value;
        return 1;
    }
    if (m->state[at] == MAP_TOMB) m->tombs--;
    m->keys[at] = key;
    m->values[at] = value;
    m->state[at] = MAP_FULL;
    m->count++;
    return 1;
}

int mx_map_del(MxMap *m, uint64_t key)
{
    if (!m || !m->cap) return 0;
    int found = 0;
    int at = map_slot(m->keys, m->state, m->cap, key, &found);
    if (!found) return 0;
    m->state[at] = MAP_TOMB;
    m->values[at] = NULL;
    m->count--;
    m->tombs++;
    return 1;
}

int mx_map_count(const MxMap *m) { return m ? m->count : 0; }

int mx_map_next(const MxMap *m, int *cursor, uint64_t *key, void **value)
{
    if (!m || !cursor) return 0;
    for (int i = *cursor; i < m->cap; i++) {
        if (m->state[i] != MAP_FULL) continue;
        *cursor = i + 1;
        if (key) *key = m->keys[i];
        if (value) *value = m->values[i];
        return 1;
    }
    *cursor = m->cap;
    return 0;
}
