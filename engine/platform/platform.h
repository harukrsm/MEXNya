// Platform primitives: lifecycle, time, paths, files, threads, window, audio device.
//
// Games only see this header, never SDL or OpenGL declarations. The desktop backend
// (SDL3) lives in engine/platform/desktop/.
#ifndef MC_PLATFORM_PLATFORM_H
#define MC_PLATFORM_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------- lifecycle
// Start-up/shutdown of the platform (window, event pump, audio device).
int plat_init(int argc, char **argv);
void plat_shutdown(void);
// 0 once the user closed the window / asked to quit.
int plat_running(void);
void plat_request_exit(void);
// Monotonic seconds and milliseconds.
double plat_time(void);
uint32_t plat_time_ms(void);
void plat_sleep_ms(int ms);

// ---------------------------------------------------------------- paths
// Read-only resource root (the folder that contains the game's assets).
int plat_asset_root(char *out, int out_size);
// Per-user writable folder (saves, settings). Created on first call.
int plat_user_dir(char *out, int out_size);
// Test/portable override for the writable folder (desktop only).
void plat_set_user_dir(const char *path);
// Application identity: names the writable folder and appears in the console.
void plat_set_identity(const char *org, const char *app);
const char *plat_app_name(void);
// Directory of the running executable (used for the console banner).
int plat_exe_dir(char *out, int out_size);

// ---------------------------------------------------------------- files
// Reads a whole file. With align > 1 the buffer is aligned to that many bytes.
// Returns NULL when missing. Caller frees with free().
void *plat_read_file(const char *path, int *size, int align);
int plat_write_file(const char *path, const void *data, int size);
// Writes to a sibling temporary file, flushes it to disk and renames it into place.
int plat_write_file_atomic(const char *path, const void *data, int size);
// Creates path exclusively and records this process. Returns a handle, or -1 when
// another live process holds it. A lock left by a dead process is reclaimed.
int plat_try_lock(const char *path);
void plat_unlock(int handle);
int plat_file_exists(const char *path);
int plat_file_size(const char *path);
int plat_remove_file(const char *path);
int plat_rename_file(const char *from, const char *to);
int plat_mkdir(const char *path);          // one level; existing is success
int plat_mkdirs(const char *path);         // create parents too
// Lists a directory; cb gets a name (no path) and is_dir. Return non-zero to stop.
int plat_dir_list(const char *path, int (*cb)(const char *name, int is_dir, void *user), void *user);

// ---------------------------------------------------------------- threads
typedef struct PlatThread PlatThread;
typedef int (*PlatThreadFn)(void *arg);
// priority: 0..255, higher runs first; desktop treats it as a hint.
PlatThread *plat_thread_start(const char *name, PlatThreadFn fn, void *arg, int priority, int stack_kb);
void plat_thread_join(PlatThread *t);
void plat_thread_detach(PlatThread *t);

typedef struct PlatMutex PlatMutex;
PlatMutex *plat_mutex_create(const char *name);
void plat_mutex_destroy(PlatMutex *m);
void plat_mutex_lock(PlatMutex *m);
void plat_mutex_unlock(PlatMutex *m);

typedef struct PlatSem PlatSem;
PlatSem *plat_sem_create(const char *name, int initial, int max);
void plat_sem_destroy(PlatSem *s);
int plat_sem_wait(PlatSem *s);
void plat_sem_signal(PlatSem *s);

// Keep the CPU's view of memory in sync with the GPU/DMA (a no-op on desktop).
void plat_flush_range(const void *p, size_t bytes);
// Bytes of generic heap still available, or 0 if the platform can't tell.
size_t plat_free_mem(void);
// Bytes of process heap currently in use (0 if the platform cannot tell).
size_t plat_used_mem(void);

// ---------------------------------------------------------------- video
// Desktop window. The software backend presents a 565 frame through this; headless
// mode never opens a window. scale is the integer pixel magnification (0 = fit).
int plat_video_open(int width, int height, int scale, int fullscreen);
void plat_video_close(void);
void plat_video_present_565(const uint16_t *pixels, int width, int height, int stride);
void plat_video_set_title(const char *title);   // before or after the window opens
// Native windows resize freely and present the frame 1:1; retro windows keep the
// frame's aspect and scale it (nearest).
void plat_video_set_native(int on);
void plat_video_set_internal(int w, int h);    // the frame size being presented
void plat_video_set_fullscreen(int on);
int plat_video_fullscreen(void);
void plat_video_toggle_fullscreen(void);
int plat_video_max_scale(void);
int plat_video_set_scale(int scale);  // preset window size: 0 = largest whole multiple that fits
// The rectangle the 480x272 frame is drawn into, in drawable pixels (any factor, centred).
void plat_video_present_rect(int dw, int dh, int *x, int *y, int *w, int *h);
int plat_video_scale(void);
int plat_video_resize_points(int w, int h);  // any size (tests); the aspect lock still applies
float plat_video_density(void);
void plat_video_snap_to_scale(void);
int plat_video_capture_window(const char *path);
int plat_video_points(int *w, int *h);
// Capture/hide the mouse while playing; release it in menus and on focus loss.
void plat_mouse_capture(int on);
int plat_mouse_captured(void);
// 0 = keyboard/mouse last used, 1 = gamepad (for on-screen prompts).
int plat_input_last_device(void);
// Key code from a name ("W", "Space", "MOUSE1", "WHEEL_UP"); see input_set_binding.
int plat_key_from_name(const char *name);
int plat_window_focused(void);
// Maps window point coordinates to the 480x272 image, including letterboxing.
void plat_map_mouse(int wx, int wy, int *gx, int *gy);
int plat_video_drawable(int *w, int *h);
void *plat_video_window(void);   // SDL_Window*, only for the GL backend
int plat_video_open_gl(int width, int height, int scale, int fullscreen);
void plat_video_swap_gl(void);
void *plat_gl_context(void);
void *plat_gl_proc(const char *name);   // OpenGL function address (after the context exists)

// The operating system's first preferred UI language ("zh-Hans-CN", "en-US", ...).
int plat_preferred_language(char *out, int size);

// ---------------------------------------------------------------- dynamic libraries
// Native plugins (.dylib/.so/.dll). NULL on failure; plat_lib_error() says why.
void *plat_lib_open(const char *path);
void *plat_lib_sym(void *lib, const char *name);
void plat_lib_close(void *lib);
const char *plat_lib_error(void);

// ---------------------------------------------------------------- audio
// Called on the platform's audio thread. Must not allocate or do file I/O.
typedef void (*PlatAudioFill)(int16_t *stereo, int frames, void *user);
// Returns 0 on success. A machine with no audio device returns non-zero and the
// game keeps running silently.
int plat_audio_open(int rate, int frames_per_block, PlatAudioFill fill, void *user);
void plat_audio_close(void);

#endif