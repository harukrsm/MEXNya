#include "engine/core/mem.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MxBlock {
    struct MxBlock *next;
    size_t cap, used;
    unsigned char data[1];
} MxBlock;

struct MxArena {
    MxBlock *head, *tail, *cur;
    size_t cap, used;
    char name[32];
};

static size_t align_up(size_t align)
{
    if (align < sizeof(void *)) align = sizeof(void *);
    size_t n = sizeof(void *);
    while (n < align) n <<= 1;
    return n;
}

static MxBlock *block_new(size_t cap)
{
    MxBlock *b = (MxBlock *)mx_mem_alloc(sizeof(MxBlock) + cap);
    if (!b) return NULL;
    b->next = NULL;
    b->cap = cap;
    b->used = 0;
    return b;
}

MxArena *mx_arena_new(size_t capacity, const char *name)
{
    if (capacity < 256) capacity = 256;
    MxArena *a = (MxArena *)mx_mem_calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->head = block_new(capacity);
    if (!a->head) {
        mx_mem_free(a);
        return NULL;
    }
    a->tail = a->head;
    a->cur = a->head;
    a->cap = capacity;
    if (name) snprintf(a->name, sizeof(a->name), "%s", name);
    return a;
}

void mx_arena_destroy(MxArena *a)
{
    if (!a) return;
    MxBlock *b = a->head;
    while (b) {
        MxBlock *next = b->next;
        mx_mem_free(b);
        b = next;
    }
    mx_mem_free(a);
}

void mx_arena_reset(MxArena *a)
{
    if (!a) return;
    for (MxBlock *b = a->head; b; b = b->next) b->used = 0;
    a->cur = a->head;
    a->used = 0;
}

void *mx_arena_alloc(MxArena *a, size_t size, size_t align)
{
    if (!a || size == 0) return NULL;
    align = align_up(align);
    for (MxBlock *b = a->cur; b; b = b->next) {
        uintptr_t base = (uintptr_t)b->data;
        uintptr_t at = (base + b->used + align - 1) & ~(uintptr_t)(align - 1);
        size_t off = (size_t)(at - base);
        if (off <= b->cap && size <= b->cap - off) {
            size_t prev = b->used;
            b->used = off + size;
            a->used += b->used - prev;
            a->cur = b;
            return b->data + off;
        }
    }
    size_t cap = a->cap;
    if (cap < size + align) cap = size + align;
    MxBlock *nb = block_new(cap);
    if (!nb) return NULL;
    a->tail->next = nb;
    a->tail = nb;
    a->cap += cap;
    a->cur = nb;
    uintptr_t base = (uintptr_t)nb->data;
    uintptr_t at = (base + align - 1) & ~(uintptr_t)(align - 1);
    size_t off = (size_t)(at - base);
    nb->used = off + size;
    a->used += off + size;
    return nb->data + off;
}

size_t mx_arena_used(const MxArena *a) { return a ? a->used : 0; }
size_t mx_arena_capacity(const MxArena *a) { return a ? a->cap : 0; }
const char *mx_arena_name(const MxArena *a) { return a && a->name[0] ? a->name : "arena"; }

static MxArena *s_frame;

void *mx_frame_alloc(size_t size)
{
    if (!s_frame) s_frame = mx_arena_new(4u * 1024u * 1024u, "frame");
    return mx_arena_alloc(s_frame, size, 16);
}

void mx_frame_reset(void) { mx_arena_reset(s_frame); }

void mx_frame_shutdown(void)
{
    mx_arena_destroy(s_frame);
    s_frame = NULL;
}

#ifdef MX_MEM_TRACK

#define MX_MEM_HEADER 64

typedef struct MxMemBlock {
    size_t size;
    const char *file;
    int line;
    struct MxMemBlock *prev, *next;
} MxMemBlock;

static MxMemBlock *s_blocks;
static size_t s_live;
static size_t s_bytes;

void *mx__mem_alloc(size_t size, const char *file, int line)
{
    MxMemBlock *b = (MxMemBlock *)malloc(MX_MEM_HEADER + (size ? size : 1));
    if (!b) return NULL;
    b->size = size;
    b->file = file;
    b->line = line;
    b->prev = NULL;
    b->next = s_blocks;
    if (s_blocks) s_blocks->prev = b;
    s_blocks = b;
    s_live++;
    s_bytes += size;
    return (unsigned char *)b + MX_MEM_HEADER;
}

void *mx__mem_calloc(size_t count, size_t size, const char *file, int line)
{
    if (count && size > (size_t)-1 / count) return NULL;
    size_t n = count * size;
    void *p = mx__mem_alloc(n, file, line);
    if (p) memset(p, 0, n);
    return p;
}

void mx__mem_free(void *ptr)
{
    if (!ptr) return;
    MxMemBlock *b = (MxMemBlock *)((unsigned char *)ptr - MX_MEM_HEADER);
    if (b->prev) b->prev->next = b->next;
    else s_blocks = b->next;
    if (b->next) b->next->prev = b->prev;
    s_live--;
    s_bytes -= b->size;
    free(b);
}

void *mx__mem_realloc(void *ptr, size_t size, const char *file, int line)
{
    if (!ptr) return mx__mem_alloc(size, file, line);
    if (size == 0) {
        mx__mem_free(ptr);
        return NULL;
    }
    MxMemBlock *old = (MxMemBlock *)((unsigned char *)ptr - MX_MEM_HEADER);
    size_t kept = old->size < size ? old->size : size;
    void *p = mx__mem_alloc(size, file, line);
    if (!p) return NULL;
    memcpy(p, ptr, kept);
    mx__mem_free(ptr);
    return p;
}

void mx_mem_report(void)
{
    if (!s_blocks) {
        fprintf(stderr, "mx_mem: no live allocations\n");
        return;
    }
    fprintf(stderr, "mx_mem: %zu live blocks, %zu bytes\n", s_live, s_bytes);
    for (MxMemBlock *b = s_blocks; b; b = b->next) {
        int seen = 0;
        for (MxMemBlock *o = s_blocks; o != b; o = o->next)
            if (o->file == b->file && o->line == b->line) seen = 1;
        if (seen) continue;
        int count = 0;
        size_t bytes = 0;
        for (MxMemBlock *o = b; o; o = o->next)
            if (o->file == b->file && o->line == b->line) {
                count++;
                bytes += o->size;
            }
        fprintf(stderr, "  %s:%d  %d block(s), %zu bytes\n", b->file ? b->file : "?", b->line, count,
                bytes);
    }
}

size_t mx_mem_used(void) { return s_bytes; }
size_t mx_mem_live_blocks(void) { return s_live; }

#else

void *mx_mem_alloc(size_t size) { return malloc(size ? size : 1); }

void *mx_mem_calloc(size_t count, size_t size) { return calloc(count, size ? size : 1); }

void *mx_mem_realloc(void *ptr, size_t size)
{
    if (!size) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

void mx_mem_free(void *ptr) { free(ptr); }
void mx_mem_report(void) {}
size_t mx_mem_used(void) { return 0; }
size_t mx_mem_live_blocks(void) { return 0; }

#endif
