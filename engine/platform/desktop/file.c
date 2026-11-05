// Desktop resource paths: assets live next to the executable (dev) or in the
// macOS bundle's Resources folder. Save data lives in the per-user pref folder.
#include "../file.h"
#include "../platform.h"

#include <stdio.h>
#include <string.h>

static char s_dir[1024];

void file_set_game_dir(const char *argv0)
{
    (void)argv0;  // the desktop finds its data from SDL_GetBasePath, not the CWD
    if (plat_asset_root(s_dir, sizeof(s_dir)) != 0)
        s_dir[0] = 0;
    size_t n = strlen(s_dir);
    if (n > 0 && s_dir[n - 1] != '/')
        snprintf(s_dir + n, sizeof(s_dir) - n, "/");
}

void file_set_data_dir(const char *path)
{
    if (!path) return;
    snprintf(s_dir, sizeof(s_dir), "%s", path);
    size_t n = strlen(s_dir);
    if (n > 0 && s_dir[n - 1] != '/')
        snprintf(s_dir + n, sizeof(s_dir) - n, "/");
}

const char *file_game_dir(void) { return s_dir; }

void file_path(char *out, int out_size, const char *rel)
{
    if (!rel)
        rel = "";
    if (rel[0] == '/')
        snprintf(out, (size_t)out_size, "%s", rel);
    else
        snprintf(out, (size_t)out_size, "%s%s", s_dir, rel);
}

void *file_load(const char *rel, int *size)
{
    char path[1400];
    file_path(path, sizeof(path), rel);
    return plat_read_file(path, size, 64);
}