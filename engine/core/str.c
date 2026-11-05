#include "engine/core/str.h"

#include "engine/core/map.h"
#include "engine/core/mem.h"

#include <stdint.h>
#include <string.h>

typedef struct MxStrNode {
    struct MxStrNode *next;
    uint64_t hash;
    char *text;
} MxStrNode;

static MxStrNode **s_buckets;
static size_t s_cap, s_count;

static int intern_grow(size_t cap)
{
    MxStrNode **buckets = (MxStrNode **)mx_mem_calloc(cap, sizeof(*buckets));
    if (!buckets) return 0;
    for (size_t i = 0; i < s_cap; i++) {
        MxStrNode *n = s_buckets[i];
        while (n) {
            MxStrNode *next = n->next;
            size_t at = (size_t)(n->hash & (uint64_t)(cap - 1));
            n->next = buckets[at];
            buckets[at] = n;
            n = next;
        }
    }
    mx_mem_free(s_buckets);
    s_buckets = buckets;
    s_cap = cap;
    return 1;
}

const char *mx_str_intern(const char *s)
{
    if (!s) return NULL;
    if (!s_cap && !intern_grow(256)) return NULL;
    uint64_t hash = mx_hash_str(s);
    size_t at = (size_t)(hash & (uint64_t)(s_cap - 1));
    for (MxStrNode *n = s_buckets[at]; n; n = n->next)
        if (n->hash == hash && strcmp(n->text, s) == 0) return n->text;
    if ((s_count + 1) * 10 >= s_cap * 7 && !intern_grow(s_cap * 2)) return NULL;
    at = (size_t)(hash & (uint64_t)(s_cap - 1));
    size_t len = strlen(s);
    MxStrNode *n = (MxStrNode *)mx_mem_alloc(sizeof(*n) + len + 1);
    if (!n) return NULL;
    n->text = (char *)(n + 1);
    memcpy(n->text, s, len + 1);
    n->hash = hash;
    n->next = s_buckets[at];
    s_buckets[at] = n;
    s_count++;
    return n->text;
}

int mx_str_intern_count(void) { return (int)s_count; }

void mx_str_intern_shutdown(void)
{
    if (!s_buckets) return;
    for (size_t i = 0; i < s_cap; i++) {
        MxStrNode *n = s_buckets[i];
        while (n) {
            MxStrNode *next = n->next;
            mx_mem_free(n);
            n = next;
        }
    }
    mx_mem_free(s_buckets);
    s_buckets = NULL;
    s_cap = 0;
    s_count = 0;
}
