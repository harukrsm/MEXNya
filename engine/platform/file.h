// Loading game files from the data folder (next to the executable by default).
#ifndef MC_PLATFORM_FILE_H
#define MC_PLATFORM_FILE_H

// Remembers the directory of argv[0]; call once at startup.
void file_set_game_dir(const char *argv0);
// Override for the read-only game data root (tests, portable installs).
void file_set_data_dir(const char *path);
const char *file_game_dir(void);

// Builds "<game dir><rel>" into out.
void file_path(char *out, int out_size, const char *rel);

// Reads a whole file into a 64-byte aligned malloc'd buffer (free with free()).
// Returns NULL on failure. `size` may be NULL.
void *file_load(const char *rel, int *size);

#endif