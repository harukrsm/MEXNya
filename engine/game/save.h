#ifndef MC_GAME_SAVE_H
#define MC_GAME_SAVE_H

#include "engine/world/world.h"

int mx_save_write(const MxWorld *w, const char *slot);
int mx_save_read(MxWorld *w, const char *slot);
int mx_save_delete(const char *slot);
int mx_save_exists(const char *slot);
int mx_save_path(char *out, int size, const char *slot);

#endif
