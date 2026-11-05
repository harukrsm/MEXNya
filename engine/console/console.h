// Game-independent debug console core.
//
// Reads stdin on its own thread, queues complete lines, and runs all commands on
// the main thread at frame boundaries. Output is serialized by one mutex.
#ifndef MC_CONSOLE_CONSOLE_H
#define MC_CONSOLE_CONSOLE_H

typedef int (*ConsoleCmdFn)(int argc, char **argv);

typedef struct {
    // Game-provided queries for Lua and commands. All optional.
    void (*pos)(float out[3]);
    void (*fps)(float *fps, float *ms);
    void (*chunks)(int *loaded, int *missing);
    int (*screenshot)(const char *path);
} ConsoleHost;

int console_init(int interactive);
void console_shutdown(void);
void console_set_host(const ConsoleHost *host);
const ConsoleHost *console_host(void);

int console_register(const char *name, const char *usage, const char *help, ConsoleCmdFn fn);

// Runs one command line. Returns 0 on success, non-zero on a command/argument error.
int console_exec_line(const char *line);
int console_exec_file(const char *path);

// Drains queued stdin lines. Call once per frame on the main thread.
void console_poll(void);
int console_enabled(void);
int console_interactive(void);
int console_pending(void);
void console_request_exit(void);
int console_exit_requested(void);
int console_exit_code(void);
void console_set_exit_code(int code);

void console_print(const char *fmt, ...);
void console_print_line(const char *line);
void console_flush(void);

// Banner: byte-for-byte contents of resources/console/banner.txt when the
// terminal is wide enough; a single plain line otherwise.
void console_set_banner_path(const char *path);
void console_force_banner(int on);  // test-only: print the banner through a pipe
int console_print_banner(const char *version, const char *backend);

// Lua (desktop builds). Console commands and scripts share this state.
int console_lua_init(void);
void console_lua_shutdown(void);
int console_lua_run_file(const char *path);
int console_lua_run_string(const char *code, const char *chunkname);
int console_lua_busy(void);
void console_lua_update(void);
void console_lua_register_command(void);

#endif