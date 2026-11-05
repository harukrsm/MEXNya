#ifndef MC_GAME_SETTINGS_H
#define MC_GAME_SETTINGS_H

#include "engine/core/json.h"

#define MX_SETTINGS_VERSION 1

Json *mx_settings_load(const char *path, const Json *defaults);
int mx_settings_save(const char *path, const Json *settings);

#endif
