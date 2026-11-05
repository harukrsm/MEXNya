// Deterministic input recording/replay. The file stores one record per logic
// step, so replay is independent of render frame rate.
#ifndef MC_PLATFORM_REPLAY_H
#define MC_PLATFORM_REPLAY_H

#include "input.h"

#define MC_REPLAY_VERSION 2

int replay_record_open(const char *path, unsigned int seed);
int replay_record_write(const Input *in);
void replay_record_close(void);
int replay_record_active(void);

// Returns 0 and the stored seed on success. Call replay_play_next() for each
// logic step; it returns 0 when the recording ends.
int replay_play_open(const char *path, unsigned int *seed);
int replay_play_next(Input *out);
void replay_play_close(void);
int replay_play_active(void);

#endif