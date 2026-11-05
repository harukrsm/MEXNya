#ifndef MC_CORE_MEM_H
#define MC_CORE_MEM_H

#include <stddef.h>

typedef struct MxArena MxArena;

MxArena *mx_arena_new(size_t capacity, const char *name);
void mx_arena_destroy(MxArena *a);
void *mx_arena_alloc(MxArena *a, size_t size, size_t align);
void mx_arena_reset(MxArena *a);
size_t mx_arena_used(const MxArena *a);
size_t mx_arena_capacity(const MxArena *a);
const char *mx_arena_name(const MxArena *a);

void *mx_frame_alloc(size_t size);
void mx_frame_reset(void);
void mx_frame_shutdown(void);

void *mx_mem_alloc(size_t size);
void *mx_mem_calloc(size_t count, size_t size);
void *mx_mem_realloc(void *ptr, size_t size);
void mx_mem_free(void *ptr);
void mx_mem_report(void);
size_t mx_mem_used(void);
size_t mx_mem_live_blocks(void);

#ifdef MX_MEM_TRACK
void *mx__mem_alloc(size_t size, const char *file, int line);
void *mx__mem_calloc(size_t count, size_t size, const char *file, int line);
void *mx__mem_realloc(void *ptr, size_t size, const char *file, int line);
void mx__mem_free(void *ptr);
#define mx_mem_alloc(size) mx__mem_alloc((size), __FILE__, __LINE__)
#define mx_mem_calloc(count, size) mx__mem_calloc((count), (size), __FILE__, __LINE__)
#define mx_mem_realloc(ptr, size) mx__mem_realloc((ptr), (size), __FILE__, __LINE__)
#define mx_mem_free(ptr) mx__mem_free(ptr)
#endif

#endif
