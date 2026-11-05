#ifndef MC_CORE_PATH_H
#define MC_CORE_PATH_H

#include <stddef.h>

void mx_path_join(char *out, size_t out_size, const char *a, const char *b);
void mx_path_normalize(char *path);
int mx_path_is_subpath(const char *root, const char *path);
const char *mx_path_ext(const char *path);
int mx_path_is_absolute(const char *path);

#endif
