#include "replay.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char magic[4];
    unsigned int version;
    unsigned int seed;
} ReplayHeader;

typedef struct {
    uint64_t held, pressed, repeat;
    float move_x, move_y;
} ReplayFrame;

static FILE *s_rec;
static FILE *s_play;
static int s_play_done;

int replay_record_open(const char *path, unsigned int seed)
{
    s_rec = fopen(path, "wb");
    if (!s_rec)
        return -1;
    ReplayHeader h = {{'M', 'C', 'R', 'I'}, MC_REPLAY_VERSION, seed};
    if (fwrite(&h, sizeof(h), 1, s_rec) != 1) {
        fclose(s_rec);
        s_rec = NULL;
        return -1;
    }
    return 0;
}

int replay_record_write(const Input *in)
{
    if (!s_rec || !in)
        return -1;
    ReplayFrame f = {in->held, in->pressed, in->repeat, in->move_x, in->move_y};
    return fwrite(&f, sizeof(f), 1, s_rec) == 1 ? 0 : -1;
}

void replay_record_close(void)
{
    if (s_rec) {
        fclose(s_rec);
        s_rec = NULL;
    }
}

int replay_record_active(void) { return s_rec != NULL; }

int replay_play_open(const char *path, unsigned int *seed)
{
    replay_play_close();
    s_play = fopen(path, "rb");
    if (!s_play)
        return -1;
    ReplayHeader h;
    if (fread(&h, sizeof(h), 1, s_play) != 1 || memcmp(h.magic, "MCRI", 4) ||
        h.version != MC_REPLAY_VERSION) {
        fclose(s_play);
        s_play = NULL;
        return -2;
    }
    if (seed)
        *seed = h.seed;
    s_play_done = 0;
    return 0;
}

int replay_play_next(Input *out)
{
    if (!s_play || s_play_done)
        return 0;
    ReplayFrame f;
    if (fread(&f, sizeof(f), 1, s_play) != 1) {
        s_play_done = 1;
        return 0;
    }
    out->held = f.held;
    out->pressed = f.pressed;
    out->repeat = f.repeat;
    out->move_x = f.move_x;
    out->move_y = f.move_y;
    return 1;
}

void replay_play_close(void)
{
    if (s_play) {
        fclose(s_play);
        s_play = NULL;
    }
    s_play_done = 0;
}

int replay_play_active(void) { return s_play != NULL && !s_play_done; }