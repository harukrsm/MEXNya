// Software mixer: up to MC_AUDIO_VOICES voices of mono PCM from <data>/sound/bank.mcs
// (built by engine/tools/make_soundbank.py, which also writes the id enum), resampled
// into one 44.1 kHz stereo stream. Without a bank the device still opens (no sounds).
#ifndef MC_AUDIO_AUDIO_H
#define MC_AUDIO_AUDIO_H

int audio_init(void);   // 0 = ready; negative = the bank file is damaged

// Fire-and-forget. volume 0..1, pan -1 (left) .. 1 (right), pitch 1 = original.
void audio_play(int id, float volume, float pan, float pitch);

// Looping voice; returns a handle (or -1). Adjust it every frame with audio_set.
int audio_loop(int id, float volume);
void audio_set(int handle, float volume, float pan, float pitch);
void audio_stop(int handle);

void audio_set_master(float volume);

// The game installs its music mixer here; the engine callback stays allocation-free.
void audio_set_music_mixer(void (*mix)(int *stereo, int frames));

// 1 when the platform had no audio device (the game keeps running).
int audio_is_silent(void);

// Debug builds: write the last 6 s of mixer output as a WAV.
void audio_debug_dump(const char *path);

#endif
