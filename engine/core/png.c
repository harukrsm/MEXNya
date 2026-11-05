#include "png.h"
#include "engine/platform/platform.h"

#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char *d; size_t n, cap; unsigned int bits; int nbits; } Out;

static void out_byte(Out *o, unsigned char b)
{
    if (o->n + 1 > o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 65536;
        unsigned char *d = (unsigned char *)realloc(o->d, cap);
        if (!d) return;
        o->d = d;
        o->cap = cap;
    }
    o->d[o->n++] = b;
}

static void put_bits(Out *o, unsigned int v, int n)  // LSB first
{
    o->bits |= v << o->nbits;
    o->nbits += n;
    while (o->nbits >= 8) {
        out_byte(o, (unsigned char)(o->bits & 0xFF));
        o->bits >>= 8;
        o->nbits -= 8;
    }
}

static void put_code(Out *o, unsigned int code, int len)  // Huffman codes go MSB first
{
    unsigned int r = 0;
    for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
    put_bits(o, r, len);
}

static void lit(Out *o, int c)
{
    if (c < 144) put_code(o, 0x30u + (unsigned)c, 8);
    else if (c < 256) put_code(o, 0x190u + (unsigned)(c - 144), 9);
    else if (c < 280) put_code(o, (unsigned)(c - 256), 7);
    else put_code(o, 0xC0u + (unsigned)(c - 280), 8);
}

static const unsigned short LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                         35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const unsigned char LEXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                         3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const unsigned short DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                         257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                         8193, 12289, 16385, 24577};
static const unsigned char DEXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                         7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void match(Out *o, int len, int dist)
{
    int li = 28;
    while (LBASE[li] > len) li--;
    lit(o, 257 + li);
    if (LEXTRA[li]) put_bits(o, (unsigned)(len - LBASE[li]), LEXTRA[li]);
    int di = 29;
    while (DBASE[di] > dist) di--;
    put_code(o, (unsigned)di, 5);
    if (DEXTRA[di]) put_bits(o, (unsigned)(dist - DBASE[di]), DEXTRA[di]);
}

#define HBITS 15
#define WIN 32768

static void deflate_fixed(Out *o, const unsigned char *src, int n)
{
    int *head = (int *)malloc(sizeof(int) << HBITS);
    if (!head) return;
    for (int i = 0; i < (1 << HBITS); i++) head[i] = -1;
    put_bits(o, 1, 1);  // final block
    put_bits(o, 1, 2);  // fixed Huffman
    int i = 0;
    while (i < n) {
        int best = 0, dist = 0;
        if (i + 3 <= n) {
            unsigned int h = ((unsigned)src[i] << 10 ^ (unsigned)src[i + 1] << 5 ^ src[i + 2]) & ((1u << HBITS) - 1);
            int cand = head[h];
            head[h] = i;
            if (cand >= 0 && i - cand <= WIN) {
                int max = n - i < 258 ? n - i : 258, l = 0;
                while (l < max && src[cand + l] == src[i + l]) l++;
                if (l >= 3) { best = l; dist = i - cand; }
            }
        }
        if (best) {
            match(o, best, dist);
            for (int k = 1; k < best && i + k + 3 <= n; k++) {
                unsigned int h = ((unsigned)src[i + k] << 10 ^ (unsigned)src[i + k + 1] << 5 ^ src[i + k + 2]) & ((1u << HBITS) - 1);
                head[h] = i + k;
            }
            i += best;
        } else {
            lit(o, src[i]);
            i++;
        }
    }
    lit(o, 256);
    if (o->nbits) put_bits(o, 0, 8 - o->nbits);
    free(head);
}

static unsigned int crc_table[256];
static void crc_init(void)
{
    for (unsigned int n = 0; n < 256; n++) {
        unsigned int c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static unsigned int crc(unsigned int c, const unsigned char *b, size_t n)
{
    c ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void be32(unsigned char *p, unsigned int v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

static void chunk(Out *f, const char *type, const unsigned char *data, size_t n)
{
    unsigned char hdr[8];
    be32(hdr, (unsigned int)n);
    memcpy(hdr + 4, type, 4);
    for (int i = 0; i < 8; i++) out_byte(f, hdr[i]);
    for (size_t i = 0; i < n; i++) out_byte(f, data[i]);
    unsigned int c = crc(0, hdr + 4, 4);
    c = crc(c, data, n);
    unsigned char t[4];
    be32(t, c);
    for (int i = 0; i < 4; i++) out_byte(f, t[i]);
}

int png_save_rgb(const char *path, const unsigned char *rgb, int w, int h)
{
    if (!path || !rgb || w <= 0 || h <= 0) return -1;
    crc_init();
    size_t row = (size_t)w * 3 + 1, raw_n = row * (size_t)h;
    unsigned char *raw = (unsigned char *)malloc(raw_n);
    if (!raw) return -1;
    for (int y = 0; y < h; y++) {
        raw[row * y] = 0;
        memcpy(raw + row * y + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    Out z = {0};
    out_byte(&z, 0x78);
    out_byte(&z, 0x01);
    deflate_fixed(&z, raw, (int)raw_n);
    unsigned int a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
    unsigned char ad[4];
    be32(ad, b << 16 | a);
    for (int i = 0; i < 4; i++) out_byte(&z, ad[i]);
    free(raw);

    Out f = {0};
    static const unsigned char SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (int i = 0; i < 8; i++) out_byte(&f, SIG[i]);
    unsigned char ihdr[13];
    be32(ihdr, (unsigned)w);
    be32(ihdr + 4, (unsigned)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(&f, "IHDR", ihdr, 13);
    chunk(&f, "IDAT", z.d, z.n);
    chunk(&f, "IEND", NULL, 0);
    free(z.d);
    int r = f.d ? plat_write_file_atomic(path, f.d, (int)f.n) : -1;
    free(f.d);
    return r;
}

int png_save_565(const char *path, const unsigned short *px, int w, int h, int stride)
{
    if (!px || w <= 0 || h <= 0) return -1;
    unsigned char *rgb = (unsigned char *)malloc((size_t)w * h * 3);
    if (!rgb) return -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned short c = px[(size_t)y * stride + x];
            unsigned char *o = rgb + ((size_t)y * w + x) * 3;
            int r = c & 0x1F, g = (c >> 5) & 0x3F, b = (c >> 11) & 0x1F;
            o[0] = (unsigned char)(r << 3 | r >> 2);
            o[1] = (unsigned char)(g << 2 | g >> 4);
            o[2] = (unsigned char)(b << 3 | b >> 2);
        }
    int r = png_save_rgb(path, rgb, w, h);
    free(rgb);
    return r;
}
