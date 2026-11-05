#ifndef MC_CORE_STR_H
#define MC_CORE_STR_H

#include <stddef.h>

const char *mx_str_intern(const char *s);
int mx_str_intern_count(void);
void mx_str_intern_shutdown(void);

#endif
