#include "console.h"
#include "engine/engine.h"
#include "engine/platform/platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#define MAX_CMDS 96
#define MAX_QUEUE 64
#define MAX_LINE 512
#define LOG_LINES 120

typedef struct {
    char name[24];
    char usage[64];
    char help[160];
    ConsoleCmdFn fn;
} Cmd;

static Cmd s_cmds[MAX_CMDS];
static int s_ncmds;
static int s_enabled, s_interactive;
static PlatMutex *s_lock;
static char s_queue[MAX_QUEUE][MAX_LINE];
static int s_qhead, s_qtail;
static volatile int s_quit;
static int s_exit_code;
static char s_log[LOG_LINES][MAX_LINE];
static int s_log_at, s_log_n;
static char s_banner_path[1024];
static ConsoleHost s_console_host;
static int s_force_banner;

// ---------------------------------------------------------------- output
void console_print_line(const char *line)
{
    if (s_lock) plat_mutex_lock(s_lock);
    fputs(line, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    snprintf(s_log[s_log_at], sizeof(s_log[s_log_at]), "%s", line);
    s_log_at = (s_log_at + 1) % LOG_LINES;
    if (s_log_n < LOG_LINES) s_log_n++;
    if (s_lock) plat_mutex_unlock(s_lock);
}

void console_print(const char *fmt, ...)
{
    char buf[MAX_LINE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console_print_line(buf);
}

void console_flush(void) { fflush(stdout); }

int console_enabled(void) { return s_enabled; }
int console_interactive(void) { return s_interactive; }
int console_pending(void) { return s_qhead != s_qtail; }
int console_exit_requested(void) { return s_quit; }
int console_exit_code(void) { return s_exit_code; }
void console_set_exit_code(int code) { s_exit_code = code; }
void console_set_host(const ConsoleHost *host) { if (host) s_console_host = *host; }
const ConsoleHost *console_host(void) { return &s_console_host; }

// ---------------------------------------------------------------- input thread
static void push_line(const char *line)
{
    if (!s_lock) return;
    plat_mutex_lock(s_lock);
    int next = (s_qtail + 1) % MAX_QUEUE;
    if (next != s_qhead) {
        snprintf(s_queue[s_qtail], MAX_LINE, "%s", line);
        s_qtail = next;
    }
    plat_mutex_unlock(s_lock);
}

static int stdin_thread(void *unused)
{
    (void)unused;
    char line[MAX_LINE];
    size_t n = 0;
    int ch;
    while (!s_quit) {
        ch = fgetc(stdin);
        if (ch == EOF) {
            if (n > 0) { line[n] = 0; push_line(line); }
            break;
        }
        if (ch == '\n' || ch == '\r') {
            if (ch == '\r') {
                int c2 = fgetc(stdin);
                if (c2 != '\n' && c2 != EOF) ungetc(c2, stdin);
            }
            line[n] = 0;
            push_line(line);
            n = 0;
        } else if (n + 1 < sizeof(line)) {
            line[n++] = (char)ch;
        }
    }
    return 0;
}

void console_poll(void)
{
    if (!s_enabled) return;
    char line[MAX_LINE];
    for (;;) {
        int got = 0;
        if (s_lock) plat_mutex_lock(s_lock);
        if (s_qhead != s_qtail) {
            snprintf(line, sizeof(line), "%s", s_queue[s_qhead]);
            s_qhead = (s_qhead + 1) % MAX_QUEUE;
            got = 1;
        }
        if (s_lock) plat_mutex_unlock(s_lock);
        if (!got) break;
        console_exec_line(line);
    }
}

void console_request_exit(void) { s_quit = 1; plat_request_exit(); }

// ---------------------------------------------------------------- parsing
static int split_args(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '#') break;
        char quote = 0;
        if (*p == '"' || *p == '\'') quote = *p++;
        char *out = p;
        char *w = p;
        while (*p) {
            if (quote && *p == quote) { p++; break; }
            if (!quote && (*p == ' ' || *p == '\t')) break;
            if (*p == '\\' && p[1]) { p++; *w++ = *p++; continue; }
            *w++ = *p++;
        }
        int end = *p;
        *w = 0;
        if (argc < max) argv[argc++] = out;
        if (!end) break;
        p++;
    }
    return argc;
}

int console_register(const char *name, const char *usage, const char *help, ConsoleCmdFn fn)
{
    if (s_ncmds >= MAX_CMDS || !name || !fn) return -1;
    Cmd *c = &s_cmds[s_ncmds++];
    snprintf(c->name, sizeof(c->name), "%s", name);
    snprintf(c->usage, sizeof(c->usage), "%s", usage ? usage : "");
    snprintf(c->help, sizeof(c->help), "%s", help ? help : "");
    c->fn = fn;
    return 0;
}

// ---------------------------------------------------------------- built-ins
static int cmd_help(int argc, char **argv)
{
    console_print("commands:");
    for (int i = 0; i < s_ncmds; i++) {
        if (argc > 1 && !strstr(s_cmds[i].name, argv[1])) continue;
        console_print("  %-10s %-22s %s", s_cmds[i].name, s_cmds[i].usage, s_cmds[i].help);
    }
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
    return 0;
}

static int cmd_quit(int argc, char **argv)
{
    (void)argc; (void)argv;
    console_request_exit();
    return 0;
}

static int cmd_version(int argc, char **argv)
{
    (void)argc; (void)argv;
    console_print("%s (MEXNya engine %s)", plat_app_name(), MX_ENGINE_VERSION);
    return 0;
}

static int cmd_log(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 20;
    if (n < 1) n = 1;
    if (n > LOG_LINES) n = LOG_LINES;
    int start = (s_log_at - (s_log_n < n ? s_log_n : n) + LOG_LINES) % LOG_LINES;
    for (int i = 0; i < (s_log_n < n ? s_log_n : n); i++)
        console_print_line(s_log[(start + i) % LOG_LINES]);
    return 0;
}

static int cmd_exec(int argc, char **argv)
{
    if (argc < 2) {
        console_print("usage: exec <file>");
        return 2;
    }
    return console_exec_file(argv[1]);
}

int console_exec_line(const char *line)
{
    char buf[MAX_LINE];
    snprintf(buf, sizeof(buf), "%s", line);
    char *argv[16];
    int argc = split_args(buf, argv, 16);
    if (argc == 0) return 0;
    // "lua" is handled by the Lua module when present.
    for (int i = 0; i < s_ncmds; i++) {
        if (strcmp(s_cmds[i].name, argv[0]) == 0)
            return s_cmds[i].fn(argc, argv);
    }
    console_print("error: unknown command '%s' (try help)", argv[0]);
    return 2;
}

int console_exec_file(const char *path)
{
    int size = 0;
    char *buf = plat_read_file(path, &size, 1);
    if (!buf) {
        console_print("error: cannot read %s", path);
        return 2;
    }
    int rc = 0;
    char *p = buf;
    while (p && *p) {
        char *e = strchr(p, '\n');
        if (e) *e = 0;
        char *line = p;
        while (*line == ' ' || *line == '\t') line++;
        if (*line && *line != '#') {
            int r = console_exec_line(line);
            if (r != 0) rc = r;
        }
        p = e ? e + 1 : NULL;
    }
    free(buf);
    return rc;
}

// ---------------------------------------------------------------- banner
void console_force_banner(int on) { s_force_banner = on; }

void console_set_banner_path(const char *path)
{
    if (path) snprintf(s_banner_path, sizeof(s_banner_path), "%s", path);
}

static int terminal_columns(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        return info.srWindow.Right - info.srWindow.Left + 1;
    return 0;
#else
    struct winsize ws;
    if (ioctl(fileno(stdout), TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 0;
#endif
}

// Colour for the banner, matching engine/tools/mexnya.py: the title's rainbow from left to
// right, light shade blocks dimmer, each row a little darker. Only on a real terminal and
// never with NO_COLOR, so piped output (and the byte-for-byte banner test) stays plain.
static int banner_colour(void)
{
    if (getenv("NO_COLOR")) return 0;
#ifdef _WIN32
    return 0;  // the GUI-subsystem build has no VT console to colour
#else
    if (!isatty(fileno(stdout))) return 0;
    const char *ct = getenv("COLORTERM");
    return ct && (strstr(ct, "truecolor") || strstr(ct, "24bit")) ? 24 : 8;
#endif
}

static void banner_fg(int mode, unsigned int rgb)
{
    int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    if (mode == 24) { printf("\x1b[38;2;%d;%d;%dm", r, g, b); return; }
#define Q(v) ((v) < 48 ? 0 : ((v) < 115 ? 1 : ((v) - 35) / 40))
    printf("\x1b[38;5;%dm", 16 + 36 * Q(r) + 6 * Q(g) + Q(b));
#undef Q
}

static unsigned int banner_gradient(float t, float k)
{
    static const unsigned int RAIN[5] = {0x2E6FA8, 0x5C9B57, 0xD08A3C, 0xC0392B, 0x7A4E8C};
    t = (t < 0 ? 0 : (t > 1 ? 1 : t)) * 4.0f;
    int i = (int)t > 3 ? 3 : (int)t;
    float f = t - i;
    unsigned int a = RAIN[i], b = RAIN[i + 1], out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float v = (((a >> sh) & 255) + ((((b >> sh) & 255) - (float)((a >> sh) & 255)) * f)) * k;
        out |= (unsigned int)(v < 0 ? 0 : (v > 255 ? 255 : v)) << sh;
    }
    return out;
}

static void banner_write_colour(const char *text, int size, int mode)
{
    int width = 1, col = 0;
    for (int i = 0; i < size; i++) {  // widest line, in cells
        if (text[i] == '\n') { col = 0; continue; }
        if (((unsigned char)text[i] & 0xC0) != 0x80 && ++col > width) width = col;
    }
    int row = 0;
    col = 0;
    for (int i = 0; i < size;) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n') { fputs("\x1b[0m\n", stdout); row++; col = 0; i++; continue; }
        int len = c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : (c >= 0xC0 ? 2 : 1));
        if (c == ' ') { fputc(' ', stdout); col++; i++; continue; }
        float k = 1.0f - 0.05f * row;
        // U+2591..2593 (light, medium, dark shade) are the banner's dim pixels.
        if (len == 3 && c == 0xE2 && (unsigned char)text[i + 1] == 0x96) {
            unsigned char d = (unsigned char)text[i + 2];
            if (d == 0x91) k *= 0.45f; else if (d == 0x92) k *= 0.62f; else if (d == 0x93) k *= 0.8f;
        }
        banner_fg(mode, banner_gradient((float)col / width, k));
        fwrite(text + i, 1, (size_t)len, stdout);
        col++;
        i += len;
    }
    fputs("\x1b[0m", stdout);
}

int console_print_banner(const char *version, const char *backend)
{
    if (!s_enabled) return 0;
    if (!s_force_banner) {
#ifndef _WIN32
        if (!isatty(fileno(stdout))) return 0;  // redirected output must stay clean
#else
        if (!_isatty(_fileno(stdout))) return 0;
#endif
    }
    int width = terminal_columns();
    int size = 0;
    char *banner = s_banner_path[0] ? plat_read_file(s_banner_path, &size, 1) : NULL;
    int fits = 0;
    if (banner) {
        fits = 1;
        int col = 0;
        for (int i = 0; i < size; i++) {
            if (banner[i] == '\n') { col = 0; continue; }
            // UTF-8 continuation bytes do not consume a cell.
            if (((unsigned char)banner[i] & 0xC0) != 0x80) col++;
            if (width > 0 && col > width) { fits = 0; break; }
        }
    }
    int colour = banner_colour();
    if (banner && fits && colour) {
        banner_write_colour(banner, size, colour);
        if (size > 0 && banner[size - 1] != '\n') fputc('\n', stdout);
    } else if (banner && fits) {
        fwrite(banner, 1, (size_t)size, stdout);
        if (size > 0 && banner[size - 1] != '\n') fputc('\n', stdout);
    } else if (banner) {
        fprintf(stdout, "%s\n", plat_app_name());
    }
    free(banner);
    console_print("%s %s", plat_app_name(), version);
    console_print("renderer: %s", backend);
    console_print("type help for commands");
    return 0;
}

// ---------------------------------------------------------------- init
int console_init(int interactive)
{
#ifdef _WIN32
    if (interactive) {
        if (!GetConsoleWindow()) {
            if (!AttachConsole(ATTACH_PARENT_PROCESS))
                AllocConsole();
        }
        FILE *out = freopen("CONOUT$", "w", stdout);
        FILE *err = freopen("CONOUT$", "w", stderr);
        FILE *in = freopen("CONIN$", "r", stdin);
        (void)out; (void)err; (void)in;
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    s_enabled = 1;
    s_interactive = interactive;
    s_lock = plat_mutex_create("console");
    s_qhead = s_qtail = 0;
    s_quit = 0;
    s_exit_code = 0;
    console_register("help", "[name]", "list commands", cmd_help);
    console_register("clear", "", "clear the terminal", cmd_clear);
    console_register("quit", "", "save and exit", cmd_quit);
    console_register("version", "", "version and build", cmd_version);
    console_register("log", "[n]", "show the last console lines", cmd_log);
    console_register("exec", "<file>", "run a console command file", cmd_exec);
    if (interactive) {
        PlatThread *t = plat_thread_start("console_stdin", stdin_thread, NULL, 0x20, 16);
        if (t) plat_thread_detach(t);
    }
    return 0;
}

void console_shutdown(void)
{
    s_quit = 1;
    s_enabled = 0;
    if (s_lock) {
        plat_mutex_destroy(s_lock);
        s_lock = NULL;
    }
}