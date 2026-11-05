// Software mixer: up to MC_AUDIO_VOICES voices of mono PCM from sound/bank.mcs
// (engine/tools/make_soundbank.py), resampled into one 44.1 kHz stereo output stream.
// The platform owns the device thread; this file never does file I/O in the callback.
#include "audio.h"
#include "audio_config.h"
#include "engine/platform/file.h"
#include "engine/platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_RATE  44100
#define MAX_BLOCK 8192

typedef struct {
    unsigned int offset, samples, rate, flags;
} BankEntry;

typedef struct {
    const short *data;
    unsigned int len;
    unsigned int pos;        // 16.16 fixed point
    unsigned int step;       // 16.16
    int vol_l, vol_r;        // 0..256
    int loops;
    int id;
    volatile int active;
} Voice;

static unsigned char *s_bank;
static const BankEntry *s_entries;
static unsigned int s_count;
static Voice s_voices[MC_AUDIO_VOICES];
static int s_master = 256;
static int s_silent;
static void (*s_music_mix)(int *, int);
static int s_mix[MAX_BLOCK * 2];

#if MC_DEBUG_AUTOSHOT
// Debug: the last few seconds of mixer output, so scripted runs can be checked by ear (or by script).
#define CAPTURE_FRAMES (OUT_RATE * 6)
static short *s_capture;
static unsigned int s_capture_pos;
#endif

static void params(Voice *v, int id, float volume, float pan, float pitch)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    float l = volume * (pan > 0.0f ? 1.0f - pan : 1.0f);
    float r = volume * (pan < 0.0f ? 1.0f + pan : 1.0f);
    v->vol_l = (int)(l * 256.0f);
    v->vol_r = (int)(r * 256.0f);
    float rate = (float)s_entries[id].rate / OUT_RATE * pitch;
    v->step = (unsigned int)(rate * 65536.0f);
}

// Audio-thread callback: no allocation, no file I/O, no long locks.
static void audio_fill(int16_t *out, int frames, void *user)
{
    (void)user;
    if (frames > MAX_BLOCK)
        frames = MAX_BLOCK;
    memset(s_mix, 0, (size_t)frames * 2 * sizeof(int));
    for (int i = 0; i < MC_AUDIO_VOICES; i++) {
        Voice *v = &s_voices[i];
        if (!v->active)
            continue;
        unsigned int pos = v->pos, step = v->step, end = v->len << 16;
        int vl = v->vol_l, vr = v->vol_r;
        for (int f = 0; f < frames; f++) {
            if (pos >= end) {
                if (!v->loops) {
                    v->active = 0;
                    break;
                }
                pos -= end;
            }
            // Linear interpolation: pitched-up loops alias far less than nearest-sample.
            unsigned int i0 = pos >> 16;
            int a = v->data[i0];
            int b = i0 + 1 < v->len ? v->data[i0 + 1] : (v->loops ? v->data[0] : a);
            int s = a + (((b - a) * (int)(pos & 0xFFFF)) >> 16);
            s_mix[f * 2] += s * vl;
            s_mix[f * 2 + 1] += s * vr;
            pos += step;
        }
        v->pos = pos;
    }
    if (s_music_mix)
        s_music_mix(s_mix, frames);
    for (int k = 0; k < frames * 2; k++) {
        int s = (s_mix[k] >> 8) * s_master >> 8;
        out[k] = (short)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
    }
#if MC_DEBUG_AUTOSHOT
    if (s_capture) {
        for (int k = 0; k < frames; k++) {
            unsigned int at = (s_capture_pos + k) % CAPTURE_FRAMES;
            s_capture[at * 2] = out[k * 2];
            s_capture[at * 2 + 1] = out[k * 2 + 1];
        }
        s_capture_pos = (s_capture_pos + frames) % CAPTURE_FRAMES;
    }
#endif
}

static int bank_reject(int code)
{
    free(s_bank);
    s_bank = NULL;
    s_entries = NULL;
    s_count = 0;
    return code;
}

int audio_init(void)
{
    int size = 0;
    s_bank = file_load("sound/bank.mcs", &size);
    int no_bank = !s_bank;
    if (no_bank) {
        // No sounds yet: the device still opens, so music mixers and later banks work.
        s_count = 0;
    } else if (size < 8 || memcmp(s_bank, "MCS1", 4))
        return bank_reject(-1);
    if (no_bank) goto open_device;
    s_count = *(unsigned int *)(s_bank + 4);
    s_entries = (const BankEntry *)(s_bank + 8);
    if (s_count < 1 || s_count > 4096)
        return bank_reject(-2);
    if ((size_t)8 + (size_t)s_count * sizeof(BankEntry) > (size_t)size)
        return bank_reject(-3);
    for (unsigned int i = 0; i < s_count; i++) {
        if ((size_t)s_entries[i].offset + (size_t)s_entries[i].samples * 2 > (size_t)size)
            return bank_reject(-4);
    }
open_device:
#if MC_DEBUG_AUTOSHOT
    s_capture = malloc(CAPTURE_FRAMES * 2 * sizeof(short));
    if (s_capture)
        memset(s_capture, 0, CAPTURE_FRAMES * 2 * sizeof(short));
#endif
    if (plat_audio_open(OUT_RATE, 1024, audio_fill, NULL) != 0) {
        // No speakers / no audio device: keep the game running, silently.
        s_silent = 1;
        fprintf(stderr, "audio: no output device, continuing silently\n");
        return 0;
    }
    return 0;
}

static int start(int id, float volume, float pan, float pitch, int loops)
{
    if (!s_bank || id < 0 || (unsigned int)id >= s_count)
        return -1;
    for (int i = 0; i < MC_AUDIO_VOICES; i++) {
        Voice *v = &s_voices[i];
        if (v->active)
            continue;
        v->data = (const short *)(s_bank + s_entries[id].offset);
        v->len = s_entries[id].samples;
        v->pos = 0;
        v->loops = loops;
        v->id = id;
        params(v, id, volume, pan, pitch);
        v->active = 1;  // last: the audio thread may pick it up right away
        return i;
    }
    return -1;
}

void audio_play(int id, float volume, float pan, float pitch)
{
    start(id, volume, pan, pitch, 0);
}

int audio_loop(int id, float volume)
{
    return start(id, volume, 0.0f, 1.0f, 1);
}

void audio_set(int handle, float volume, float pan, float pitch)
{
    if (handle < 0 || handle >= MC_AUDIO_VOICES || !s_voices[handle].active)
        return;
    Voice *v = &s_voices[handle];
    params(v, v->id, volume, pan, pitch);
}

void audio_stop(int handle)
{
    if (handle >= 0 && handle < MC_AUDIO_VOICES)
        s_voices[handle].active = 0;
}

void audio_set_music_mixer(void (*mix)(int *, int))
{
    s_music_mix = mix;
}

void audio_set_master(float volume)
{
    s_master = (int)((volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume)) * 256.0f);
}

int audio_is_silent(void) { return s_silent; }

#if MC_DEBUG_AUTOSHOT
void audio_debug_dump(const char *path)
{
    if (!s_capture)
        return;
    unsigned int bytes = CAPTURE_FRAMES * 4;
    unsigned char hdr[44];
    unsigned int *h = (unsigned int *)hdr;
    h[0] = 0x46464952; h[1] = 36 + bytes; h[2] = 0x45564157; h[3] = 0x20746D66;
    h[4] = 16; h[5] = 0x00020001; h[6] = OUT_RATE; h[7] = OUT_RATE * 4;
    h[8] = 0x00100004; h[9] = 0x61746164; h[10] = bytes;
    int size = (int)(sizeof(hdr) + bytes);
    unsigned char *buf = malloc((size_t)size);
    if (!buf)
        return;
    memcpy(buf, hdr, sizeof(hdr));
    unsigned int pos = s_capture_pos;
    memcpy(buf + 44, s_capture + pos * 2, (size_t)(CAPTURE_FRAMES - pos) * 4);
    memcpy(buf + 44 + (CAPTURE_FRAMES - pos) * 4, s_capture, (size_t)pos * 4);
    plat_write_file(path, buf, size);
    free(buf);
}
#endif