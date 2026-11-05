// SDL3 platform: clock, threads, files, window, input and audio device.
// The API is engine/platform/platform.h; SDL types stay in this file.
#include "../platform.h"
#include "../input.h"
#include "engine/render/render_config.h"

#include <SDL3/SDL.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

// ---------------------------------------------------------------- clock
double plat_time(void)
{
    static Uint64 freq;
    if (!freq)
        freq = SDL_GetPerformanceFrequency();
    return (double)SDL_GetPerformanceCounter() / (double)freq;
}

uint32_t plat_time_ms(void) { return (uint32_t)(plat_time() * 1000.0); }

void plat_sleep_ms(int ms)
{
    if (ms > 0)
        SDL_Delay((Uint32)ms);
}

// ---------------------------------------------------------------- paths
int plat_exe_dir(char *out, int out_size)
{
    const char *base = SDL_GetBasePath();
    if (!base) {
        if (out_size > 0) out[0] = 0;
        return -1;
    }
    SDL_strlcpy(out, base, (size_t)out_size);
    size_t n = strlen(out);
    while (n > 1 && (out[n - 1] == '/' || out[n - 1] == '\\'))
        out[--n] = 0;
    return 0;
}

int plat_asset_root(char *out, int out_size)
{
    char base[1024], probe[1200];
    if (plat_exe_dir(base, sizeof(base)) != 0)
        return -1;
    // Development build: <build>/assets. macOS app: <bundle>/Contents/Resources/assets.
    snprintf(probe, sizeof(probe), "%s/assets", base);
    struct stat st;
    if (stat(probe, &st) == 0 && (st.st_mode & S_IFDIR)) {
        snprintf(out, (size_t)out_size, "%s", probe);
        return 0;
    }
    snprintf(probe, sizeof(probe), "%s/../Resources/assets", base);
    if (stat(probe, &st) == 0 && (st.st_mode & S_IFDIR)) {
        snprintf(out, (size_t)out_size, "%s", probe);
        return 0;
    }
    snprintf(out, (size_t)out_size, "%s/assets", base);
    return 0;
}

static char s_user_override[1024];
static char s_org[64] = "MEXNya";
static char s_app[64] = "Engine";
void plat_set_identity(const char *org, const char *app)
{
    snprintf(s_org, sizeof(s_org), "%s", org ? org : "MEXNya");
    snprintf(s_app, sizeof(s_app), "%s", app ? app : "Engine");
}
const char *plat_app_name(void) { return s_app; }
void plat_set_user_dir(const char *path) { snprintf(s_user_override, sizeof(s_user_override), "%s", path ? path : ""); }

int plat_user_dir(char *out, int out_size)
{
    if (s_user_override[0]) {
        snprintf(out, (size_t)out_size, "%s", s_user_override);
        plat_mkdirs(out);
        return 0;
    }
    char *p = SDL_GetPrefPath(s_org, s_app);
    if (!p) {
        if (out_size > 0) out[0] = 0;
        return -1;
    }
    SDL_strlcpy(out, p, (size_t)out_size);
    SDL_free(p);
    plat_mkdirs(out);
    return 0;
}

// ---------------------------------------------------------------- files
void *plat_read_file(const char *path, int *size, int align)
{
    (void)align;  // desktop CPUs do not need extra alignment
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    void *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (n > 0 && (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n)) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (size)
        *size = (int)n;
    return buf;
}

int plat_write_file(const char *path, const void *data, int size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int ok = size == 0 || fwrite(data, 1, (size_t)size, f) == (size_t)size;
    if (fclose(f) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

int plat_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int plat_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return (int)st.st_size;
}

int plat_remove_file(const char *path) { return remove(path) == 0 ? 0 : -1; }
int plat_rename_file(const char *from, const char *to) { return rename(from, to) == 0 ? 0 : -1; }

int plat_mkdir(const char *path)
{
#ifdef _WIN32
    if (_mkdir(path) == 0 || errno == EEXIST)
        return 0;
#else
    if (mkdir(path, 0777) == 0 || errno == EEXIST)
        return 0;
#endif
    return -1;
}

int plat_mkdirs(const char *path)
{
    char tmp[1200];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t n = strlen(tmp);
    while (n > 1 && (tmp[n - 1] == '/' || tmp[n - 1] == '\\'))
        tmp[--n] = 0;
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char save = tmp[i];
            tmp[i] = 0;
            plat_mkdir(tmp);
            tmp[i] = save;
        }
    }
    return plat_mkdir(tmp);
}

int plat_dir_list(const char *path, int (*cb)(const char *name, int is_dir, void *user), void *user)
{
#ifdef _WIN32
    char pattern[1200];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (cb(fd.cFileName, is_dir, user))
            break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
#else
    DIR *d = opendir(path);
    if (!d)
        return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char full[1400];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        struct stat st;
        int is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        if (cb(e->d_name, is_dir, user))
            break;
    }
    closedir(d);
    return 0;
#endif
}

// ---------------------------------------------------------------- threads
typedef struct {
    PlatThreadFn fn;
    void *arg;
} ThreadStart;

static int SDLCALL thread_trampoline(void *data)
{
    ThreadStart s = *(ThreadStart *)data;
    free(data);
    return s.fn(s.arg);
}

PlatThread *plat_thread_start(const char *name, PlatThreadFn fn, void *arg, int priority, int stack_kb)
{
    (void)priority; (void)stack_kb;  // SDL threads use the OS defaults
    ThreadStart *s = malloc(sizeof(*s));
    if (!s)
        return NULL;
    s->fn = fn;
    s->arg = arg;
    SDL_Thread *t = SDL_CreateThread(thread_trampoline, name ? name : "mc_thread", s);
    if (!t) {
        free(s);
        return NULL;
    }
    return (PlatThread *)t;
}

void plat_thread_join(PlatThread *t)
{
    if (t)
        SDL_WaitThread((SDL_Thread *)t, NULL);
}

void plat_thread_detach(PlatThread *t)
{
    if (t)
        SDL_DetachThread((SDL_Thread *)t);
}

PlatMutex *plat_mutex_create(const char *name)
{
    (void)name;
    return (PlatMutex *)SDL_CreateMutex();
}
void plat_mutex_destroy(PlatMutex *m) { if (m) SDL_DestroyMutex((SDL_Mutex *)m); }
void plat_mutex_lock(PlatMutex *m) { if (m) SDL_LockMutex((SDL_Mutex *)m); }
void plat_mutex_unlock(PlatMutex *m) { if (m) SDL_UnlockMutex((SDL_Mutex *)m); }

PlatSem *plat_sem_create(const char *name, int initial, int max)
{
    (void)name; (void)max;
    return (PlatSem *)SDL_CreateSemaphore((Uint32)initial);
}
void plat_sem_destroy(PlatSem *s) { if (s) SDL_DestroySemaphore((SDL_Semaphore *)s); }
int plat_sem_wait(PlatSem *s) { if (!s) return -1; SDL_WaitSemaphore((SDL_Semaphore *)s); return 0; }
void plat_sem_signal(PlatSem *s) { if (s) SDL_SignalSemaphore((SDL_Semaphore *)s); }

void plat_flush_range(const void *p, size_t bytes) { (void)p; (void)bytes; }
size_t plat_free_mem(void) { return 0; }  // not meaningful on desktop
size_t plat_used_mem(void)
{
#ifdef __APPLE__
    malloc_statistics_t st;
    malloc_zone_statistics(malloc_default_zone(), &st);
    return st.size_in_use;
#else
    return 0;  // not tracked on this platform
#endif
}

// ---------------------------------------------------------------- video
static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_frame;
static SDL_GLContext s_gl;
static int s_fullscreen;
static int s_scale;
static int s_window_scale = 2;
static int s_internal_w, s_internal_h;
static int s_native;
static int s_tex_w, s_tex_h;
// The frame being presented (retro: the game's fixed size; native: the drawable).
#define FW (s_internal_w > 0 ? s_internal_w : MC_SCREEN_W)
#define FH (s_internal_h > 0 ? s_internal_h : MC_SCREEN_H)
static char s_capture_path[512];
static char s_title[128] = "MEXNya";

void plat_video_set_title(const char *title)
{
    snprintf(s_title, sizeof(s_title), "%s", title && title[0] ? title : "MEXNya");
    if (s_window) SDL_SetWindowTitle(s_window, s_title);
}

// Where the internal frame lands in a dw x dh drawable: the largest rectangle of that aspect,
// centred, rounded to whole pixels so nearest sampling stays stable frame to frame.
static void present_rect(int dw, int dh, float *x, float *y, float *w, float *h)
{
    if (s_native) { *x = 0; *y = 0; *w = (float)dw; *h = (float)dh; return; }
    float s = (float)dw / (float)FW;
    if ((float)dh / (float)FH < s) s = (float)dh / (float)FH;
    if (s <= 0.0f) s = 1.0f;
    int pw = (int)((float)FW * s + 0.5f), ph = (int)((float)FH * s + 0.5f);
    if (pw > dw) pw = dw;
    if (ph > dh) ph = dh;
    *w = (float)pw;
    *h = (float)ph;
    *x = (float)((dw - pw) / 2);
    *y = (float)((dh - ph) / 2);
}

void plat_video_present_rect(int dw, int dh, int *x, int *y, int *w, int *h)
{
    float fx, fy, fw, fh;
    present_rect(dw, dh, &fx, &fy, &fw, &fh);
    *x = (int)fx; *y = (int)fy; *w = (int)fw; *h = (int)fh;
}

static int max_scale_for_window(void)
{
    int dw = FW, dh = FH;
    int display = s_window ? SDL_GetDisplayForWindow(s_window) : SDL_GetPrimaryDisplay();
    SDL_Rect r;
    if (SDL_GetDisplayUsableBounds(display, &r)) {
        dw = r.w - 40;
        dh = r.h - 80;
    }
    int sx = dw / FW, sy = dh / FH;
    int m = sx < sy ? sx : sy;
    return m < 1 ? 1 : m;
}

int plat_video_max_scale(void) { return max_scale_for_window(); }

int plat_video_set_scale(int scale)
{
    if (!s_window || s_native)
        return -1;
    int max = max_scale_for_window();
    if (scale <= 0) scale = max;
    if (scale > max) scale = max;
    if (scale < 1) scale = 1;
    s_window_scale = scale;
    if (!s_fullscreen) {
        float density = SDL_GetWindowPixelDensity(s_window);
        int w = (int)((float)FW * scale / (density > 0 ? density : 1.0f));
        int h = (int)((float)FH * scale / (density > 0 ? density : 1.0f));
        SDL_SetWindowSize(s_window, w, h);
        SDL_SetWindowPosition(s_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_SyncWindow(s_window);
    }
    return scale;
}

int plat_video_scale(void) { return s_window_scale; }

// Tests/console: set the window to any size in points, as a drag would.
int plat_video_resize_points(int w, int h)
{
    if (!s_window || s_fullscreen) return -1;
    SDL_SetWindowSize(s_window, w, h);
    SDL_SyncWindow(s_window);
    return 0;
}
float plat_video_density(void) { return s_window ? SDL_GetWindowPixelDensity(s_window) : 1.0f; }
int plat_video_points(int *w, int *h) { if (!s_window) return -1; SDL_GetWindowSize(s_window, w, h); return 0; }
int plat_video_capture_window(const char *path)
{
    if (!s_renderer || !path) return -1;
    snprintf(s_capture_path, sizeof(s_capture_path), "%s", path);
    return 0;
}

void plat_video_snap_to_scale(void)
{
    if (!s_window || s_fullscreen || s_native) return;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    plat_video_set_scale((w + FW / 2) / FW);
}

// Native windows open at most of the usable screen, any aspect, freely resizable.
static void native_window_size(void)
{
    int w = 1440, h = 900;
    SDL_Rect r;
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(s_window), &r)) {
        w = r.w * 85 / 100;
        h = r.h * 85 / 100;
    }
    SDL_SetWindowAspectRatio(s_window, 0.0f, 0.0f);
    SDL_SetWindowMinimumSize(s_window, 800, 500);
    SDL_SetWindowSize(s_window, w, h);
    SDL_SetWindowPosition(s_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SyncWindow(s_window);
}

void plat_video_set_native(int on) { s_native = on ? 1 : 0; }

void plat_video_set_internal(int w, int h)
{
    s_internal_w = w;
    s_internal_h = h;
}

int plat_video_open(int width, int height, int scale, int fullscreen)
{
    s_internal_w = width;
    s_internal_h = height;
    s_scale = scale > 0 ? scale : 0;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return -1;
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    if (fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    s_window = SDL_CreateWindow(s_title, width * (scale > 0 ? scale : 2),
                                height * (scale > 0 ? scale : 2), flags);
    if (!s_window)
        return -2;
    // Retina: keep a 2x-of-internal window in points, so the drawable is 2x pixels.
    float density = SDL_GetWindowPixelDensity(s_window);
    if (density >= 2.0f) {
        SDL_SetWindowSize(s_window, width * (scale > 0 ? scale : 2) / 2,
                          height * (scale > 0 ? scale : 2) / 2);
    }
    if (!fullscreen)
        SDL_SetWindowPosition(s_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    s_renderer = SDL_CreateRenderer(s_window, NULL);
    if (!s_renderer)
        return -3;
    SDL_SetRenderVSync(s_renderer, 0);
    s_frame = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_BGR565, SDL_TEXTUREACCESS_STREAMING,
                                width, height);
    if (!s_frame)
        return -4;
    SDL_SetTextureScaleMode(s_frame, SDL_SCALEMODE_NEAREST);
    s_tex_w = width;
    s_tex_h = height;
    s_fullscreen = fullscreen ? 1 : 0;
    if (s_native) {
        native_window_size();
        if (fullscreen) SDL_SetWindowFullscreen(s_window, true);
        return 0;
    }
    SDL_SetWindowAspectRatio(s_window, (float)FW / (float)FH, (float)FW / (float)FH);
    SDL_SetWindowMinimumSize(s_window, FW / 2, FH / 2);
    if (fullscreen)
        SDL_SetWindowFullscreen(s_window, true);
    else
        plat_video_set_scale(scale > 0 ? scale : 0);
    return 0;
}

void plat_video_close(void)
{
    if (s_gl) { SDL_GL_DestroyContext(s_gl); s_gl = NULL; }
    if (s_frame) { SDL_DestroyTexture(s_frame); s_frame = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
    if (s_window) { SDL_DestroyWindow(s_window); s_window = NULL; }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void plat_video_present_565(const uint16_t *pixels, int width, int height, int stride)
{
    if (!s_renderer || !s_frame || !pixels)
        return;
    if (width != s_tex_w || height != s_tex_h) {
        // The frame was resized (native mode follows the window).
        SDL_DestroyTexture(s_frame);
        s_frame = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_BGR565, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!s_frame) return;
        SDL_SetTextureScaleMode(s_frame, SDL_SCALEMODE_NEAREST);
        s_tex_w = width;
        s_tex_h = height;
    }
    SDL_UpdateTexture(s_frame, NULL, pixels, stride * (int)sizeof(uint16_t));
    int dw = 0, dh = 0;
    SDL_GetWindowSizeInPixels(s_window, &dw, &dh);
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);  // letterbox is black
    SDL_RenderClear(s_renderer);
    // Fit by any factor (not just whole ones): the window keeps the internal aspect but can be any size.
    // Nearest sampling keeps the pixels hard even when some columns end up one wider.
    SDL_FRect dst;
    present_rect(dw, dh, &dst.x, &dst.y, &dst.w, &dst.h);
    SDL_RenderTexture(s_renderer, s_frame, NULL, &dst);
    if (s_capture_path[0]) {
        SDL_Surface *cap = SDL_RenderReadPixels(s_renderer, NULL);
        if (cap) {
            SDL_Surface *rgb = SDL_ConvertSurface(cap, SDL_PIXELFORMAT_BGR24);
            SDL_SaveBMP(rgb ? rgb : cap, s_capture_path);
            if (rgb) SDL_DestroySurface(rgb);
            SDL_DestroySurface(cap);
        }
        s_capture_path[0] = 0;
    }
    SDL_RenderPresent(s_renderer);
}

void plat_video_set_fullscreen(int on)
{
    if (!s_window)
        return;
    s_fullscreen = on ? 1 : 0;
    SDL_SetWindowFullscreen(s_window, on ? true : false);
    if (!on) plat_video_set_scale(s_window_scale);
}

int plat_video_fullscreen(void) { return s_fullscreen; }
void plat_video_toggle_fullscreen(void) { plat_video_set_fullscreen(!s_fullscreen); }

int plat_video_drawable(int *w, int *h)
{
    if (!s_window)
        return -1;
    SDL_GetWindowSizeInPixels(s_window, w, h);
    return 0;
}

void *plat_video_window(void) { return s_window; }

void plat_map_mouse(int wx, int wy, int *gx, int *gy)
{
    int ww = FW, wh = FH, dw = FW, dh = FH;
    if (s_window) {
        SDL_GetWindowSize(s_window, &ww, &wh);
        SDL_GetWindowSizeInPixels(s_window, &dw, &dh);
    }
    float density = ww > 0 ? (float)dw / (float)ww : 1.0f;
    float px, py, pw, ph;
    present_rect(dw, dh, &px, &py, &pw, &ph);
    int x = (int)((wx * density - px) * (float)FW / pw);
    int y = (int)((wy * density - py) * (float)FH / ph);
    if (x < 0) x = 0;
    if (x > FW - 1) x = FW - 1;
    if (y < 0) y = 0;
    if (y > FH - 1) y = FH - 1;
    *gx = x;
    *gy = y;
}

int plat_video_open_gl(int width, int height, int scale, int fullscreen)
{
    (void)scale;
    s_internal_w = width;
    s_internal_h = height;
    if (s_window || s_gl)
        plat_video_close();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    if (fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    s_window = SDL_CreateWindow(s_title, FW * 2, FH * 2, flags);
    if (!s_window)
        return -2;
    s_gl = SDL_GL_CreateContext(s_window);
    if (!s_gl)
        return -3;
    SDL_GL_MakeCurrent(s_window, s_gl);
    SDL_GL_SetSwapInterval(s_native ? 1 : 0);
    s_fullscreen = fullscreen ? 1 : 0;
    if (s_native) {
        native_window_size();
        return 0;
    }
    float density = SDL_GetWindowPixelDensity(s_window);
    if (density >= 2.0f)
        SDL_SetWindowSize(s_window, FW, FH);  // drawable is then 2x internal
    SDL_SetWindowAspectRatio(s_window, (float)FW / (float)FH, (float)FW / (float)FH);
    SDL_SetWindowMinimumSize(s_window, FW / 2, FH / 2);
    if (!fullscreen)
        plat_video_set_scale(0);
    return 0;
}

void plat_video_swap_gl(void)
{
    if (s_window)
        SDL_GL_SwapWindow(s_window);
}

void *plat_gl_context(void) { return s_gl; }
void *plat_gl_proc(const char *name) { return (void *)SDL_GL_GetProcAddress(name); }

// ---------------------------------------------------------------- audio
static SDL_AudioStream *s_stream;
static PlatAudioFill s_fill;
static void *s_fill_user;
static int16_t s_mixbuf[8192 * 2];

static void SDLCALL audio_cb(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    (void)userdata; (void)total_amount;
    if (additional_amount <= 0)
        return;
    int frames = additional_amount / 4;  // S16 stereo
    while (frames > 0) {
        int n = frames > 8192 ? 8192 : frames;
        s_fill(s_mixbuf, n, s_fill_user);
        SDL_PutAudioStreamData(stream, s_mixbuf, n * 4);
        frames -= n;
    }
}

int plat_audio_open(int rate, int frames_per_block, PlatAudioFill fill, void *user)
{
    (void)frames_per_block;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return -1;
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = rate;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    s_fill = fill;
    s_fill_user = user;
    s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, NULL);
    if (!s_stream)
        return -2;
    SDL_ResumeAudioStreamDevice(s_stream);
    return 0;
}

void plat_audio_close(void)
{
    if (s_stream) {
        SDL_DestroyAudioStream(s_stream);
        s_stream = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// ---------------------------------------------------------------- lifecycle
int plat_init(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!SDL_Init(0))
        return -1;
    return 0;
}

void plat_shutdown(void)
{
    input_shutdown();
    SDL_Quit();
}

int plat_running(void)
{
    extern int mc_plat_running;
    return mc_plat_running;
}

void plat_request_exit(void)
{
    extern int mc_plat_running;
    mc_plat_running = 0;
}

int mc_plat_running = 1;
// ---------------------------------------------------------------- dynamic libraries
void *plat_lib_open(const char *path) { return path ? (void *)SDL_LoadObject(path) : NULL; }
void *plat_lib_sym(void *lib, const char *name)
{
    return lib && name ? (void *)SDL_LoadFunction((SDL_SharedObject *)lib, name) : NULL;
}
void plat_lib_close(void *lib) { if (lib) SDL_UnloadObject((SDL_SharedObject *)lib); }
const char *plat_lib_error(void) { return SDL_GetError(); }

int plat_preferred_language(char *out, int size)
{
    int n = 0;
    SDL_Locale **loc = SDL_GetPreferredLocales(&n);
    if (!loc || n <= 0 || !loc[0]) { if (loc) SDL_free(loc); return -1; }
    if (loc[0]->country) snprintf(out, (size_t)size, "%s-%s", loc[0]->language, loc[0]->country);
    else snprintf(out, (size_t)size, "%s", loc[0]->language);
    SDL_free(loc);
    return 0;
}
