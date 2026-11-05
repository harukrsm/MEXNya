#ifndef MC_CORE_VEC_H
#define MC_CORE_VEC_H

#include <stddef.h>
#include "engine/core/mem.h"

#define MX_VEC(T) struct { T *data; int len, cap; }

static inline int mx__vec_grow(void **data, int *cap, size_t elem, int need)
{
    if (need <= *cap) return 1;
    if (need < 0 || elem == 0) return 0;
    int next = *cap > 0 ? *cap : 8;
    while (next < need) {
        if (next > 0x3FFFFFFF) {
            next = need;
            break;
        }
        next *= 2;
    }
    if ((size_t)next > (size_t)-1 / elem) return 0;
    void *p = mx_mem_realloc(*data, (size_t)next * elem);
    if (!p) return 0;
    *data = p;
    *cap = next;
    return 1;
}

#define mx_vec_reserve(v, n) mx__vec_grow((void **)&(v)->data, &(v)->cap, sizeof(*(v)->data), (n))

#define mx_vec_push(v, x) \
    (mx_vec_reserve((v), (v)->len + 1) ? ((v)->data[(v)->len++] = (x), 1) : 0)

#define mx_vec_remove_swap(v, i) \
    do { \
        int mx__i = (i); \
        if (mx__i >= 0 && mx__i < (v)->len) { \
            (v)->data[mx__i] = (v)->data[--(v)->len]; \
        } \
    } while (0)

#define mx_vec_clear(v) ((v)->len = 0)

#define mx_vec_free(v) \
    do { \
        mx_mem_free((v)->data); \
        (v)->data = NULL; \
        (v)->len = 0; \
        (v)->cap = 0; \
    } while (0)

#endif
