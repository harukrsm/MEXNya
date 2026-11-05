#include "../sysfont.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>

static CTFontRef s_font;

static const char *face_for(const char *lang)
{
    if (!strcmp(lang, "zh-Hans")) return "PingFang SC";
    if (!strcmp(lang, "zh-Hant")) return "PingFang TC";
    if (!strcmp(lang, "ja")) return "Hiragino Sans";
    if (!strcmp(lang, "ko")) return "Apple SD Gothic Neo";
    return "Helvetica Neue";
}

int sysfont_open(const char *lang, float px)
{
    sysfont_close();
    CFStringRef name = CFStringCreateWithCString(NULL, face_for(lang ? lang : "en"), kCFStringEncodingUTF8);
    s_font = CTFontCreateWithName(name, px, NULL);
    CFRelease(name);
    return s_font ? 0 : -1;
}

void sysfont_close(void)
{
    if (s_font) CFRelease(s_font);
    s_font = NULL;
}

void sysfont_metrics(float *ascent, float *descent)
{
    if (ascent) *ascent = s_font ? (float)CTFontGetAscent(s_font) : 0;
    if (descent) *descent = s_font ? (float)CTFontGetDescent(s_font) : 0;
}

float sysfont_glyph(unsigned int cp, unsigned char *cov, int max_w, int h, int baseline, int *out_w)
{
    if (!s_font) return -1;
    UniChar u[2];
    CFIndex n = 1;
    if (cp >= 0x10000) {
        cp -= 0x10000;
        u[0] = (UniChar)(0xD800 + (cp >> 10));
        u[1] = (UniChar)(0xDC00 + (cp & 0x3FF));
        n = 2;
    } else u[0] = (UniChar)cp;
    CGGlyph g[2] = {0, 0};
    CTFontRef font = s_font;
    CFRetain(font);
    if (!CTFontGetGlyphsForCharacters(font, u, g, n) || !g[0]) {
        // The face lacks it: ask CoreText for a fallback face that has it.
        CFStringRef str = CFStringCreateWithCharacters(NULL, u, n);
        CTFontRef fb = CTFontCreateForString(s_font, str, CFRangeMake(0, n));
        CFRelease(str);
        CFRelease(font);
        font = fb;
        if (!font || !CTFontGetGlyphsForCharacters(font, u, g, n) || !g[0]) { if (font) CFRelease(font); return -1; }
    }
    CGSize adv;
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, g, &adv, 1);
    int w = (int)ceilf((float)adv.width) + 2;
    if (w > max_w) w = max_w;
    if (w < 1) w = 1;
    memset(cov, 0, (size_t)w * (size_t)h);
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = CGBitmapContextCreate(cov, (size_t)w, (size_t)h, 8, (size_t)w, gray, kCGImageAlphaNone);
    CGColorSpaceRelease(gray);
    if (ctx) {
        CGContextSetGrayFillColor(ctx, 1.0, 1.0);
        CGContextSetShouldAntialias(ctx, true);
        CGContextSetShouldSmoothFonts(ctx, false);
        CGPoint pos = CGPointMake(1.0, (CGFloat)(h - baseline));  // CG's origin is bottom-left
        CTFontDrawGlyphs(font, g, &pos, 1, ctx);
        CGContextRelease(ctx);
    }
    CFRelease(font);
    *out_w = w;
    return (float)adv.width;
}

#elif defined(_WIN32)
#include <windows.h>

static HFONT s_font;
static HDC s_dc;
static HBITMAP s_bmp;
static unsigned char *s_bits;
static int s_bw, s_bh;

static const wchar_t *face_for(const char *lang)
{
    if (!strcmp(lang, "zh-Hans")) return L"Microsoft YaHei UI";
    if (!strcmp(lang, "zh-Hant")) return L"Microsoft JhengHei UI";
    if (!strcmp(lang, "ja")) return L"Yu Gothic UI";
    if (!strcmp(lang, "ko")) return L"Malgun Gothic";
    return L"Segoe UI";
}

int sysfont_open(const char *lang, float px)
{
    sysfont_close();
    s_dc = CreateCompatibleDC(NULL);
    if (!s_dc) return -1;
    s_font = CreateFontW(-(int)(px + 0.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                         face_for(lang ? lang : "en"));
    if (!s_font) { sysfont_close(); return -1; }
    SelectObject(s_dc, s_font);
    SetTextColor(s_dc, RGB(255, 255, 255));
    SetBkColor(s_dc, RGB(0, 0, 0));
    SetBkMode(s_dc, OPAQUE);
    return 0;
}

void sysfont_close(void)
{
    if (s_bmp) DeleteObject(s_bmp);
    if (s_font) DeleteObject(s_font);
    if (s_dc) DeleteDC(s_dc);
    s_bmp = NULL; s_font = NULL; s_dc = NULL; s_bits = NULL; s_bw = s_bh = 0;
}

void sysfont_metrics(float *ascent, float *descent)
{
    TEXTMETRICW tm;
    if (s_dc && GetTextMetricsW(s_dc, &tm)) {
        if (ascent) *ascent = (float)tm.tmAscent;
        if (descent) *descent = (float)tm.tmDescent;
        return;
    }
    if (ascent) *ascent = 0;
    if (descent) *descent = 0;
}

static int ensure_bitmap(int w, int h)
{
    if (s_bmp && w <= s_bw && h <= s_bh) return 0;
    if (s_bmp) DeleteObject(s_bmp);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    s_bmp = CreateDIBSection(s_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!s_bmp) return -1;
    SelectObject(s_dc, s_bmp);
    s_bits = (unsigned char *)bits;
    s_bw = w;
    s_bh = h;
    return 0;
}

float sysfont_glyph(unsigned int cp, unsigned char *cov, int max_w, int h, int baseline, int *out_w)
{
    if (!s_dc) return -1;
    wchar_t u[2];
    int n = 1;
    if (cp >= 0x10000) {
        cp -= 0x10000;
        u[0] = (wchar_t)(0xD800 + (cp >> 10));
        u[1] = (wchar_t)(0xDC00 + (cp & 0x3FF));
        n = 2;
    } else u[0] = (wchar_t)cp;
    SIZE sz;
    if (!GetTextExtentPoint32W(s_dc, u, n, &sz)) return -1;
    int w = sz.cx + 2;
    if (w > max_w) w = max_w;
    if (w < 1) w = 1;
    if (ensure_bitmap(w > 64 ? w : 64, h > 64 ? h : 64) != 0) return -1;
    memset(s_bits, 0, (size_t)s_bw * (size_t)s_bh * 4);
    TEXTMETRICW tm;
    GetTextMetricsW(s_dc, &tm);
    TextOutW(s_dc, 1, baseline - tm.tmAscent, u, n);
    GdiFlush();
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) cov[y * w + x] = s_bits[((size_t)y * s_bw + x) * 4 + 1];
    *out_w = w;
    return (float)sz.cx;
}

#else
int sysfont_open(const char *lang, float px) { (void)lang; (void)px; return -1; }
void sysfont_close(void) {}
void sysfont_metrics(float *a, float *d) { if (a) *a = 0; if (d) *d = 0; }
float sysfont_glyph(unsigned int cp, unsigned char *c, int mw, int h, int b, int *w)
{
    (void)cp; (void)c; (void)mw; (void)h; (void)b; (void)w;
    return -1;
}
#endif
