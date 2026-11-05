#ifndef MC_CORE_MAP_H
#define MC_CORE_MAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct MxMap MxMap;

uint64_t mx_hash_str(const char *s);
uint64_t mx_hash_bytes(const void *data, size_t size);

MxMap *mx_map_new(void);
void mx_map_free(MxMap *m);
void *mx_map_get(const MxMap *m, uint64_t key);
int mx_map_put(MxMap *m, uint64_t key, void *value);
int mx_map_del(MxMap *m, uint64_t key);
int mx_map_count(const MxMap *m);
int mx_map_next(const MxMap *m, int *cursor, uint64_t *key, void **value);
void mx_map_clear(MxMap *m);

#endif
