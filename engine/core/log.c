#include "engine/core/log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
static SRWLOCK s_lock = SRWLOCK_INIT;
static void log_lock(void) { AcquireSRWLockExclusive(&s_lock); }
static void log_unlock(void) { ReleaseSRWLockExclusive(&s_lock); }
#else
#include <pthread.h>
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static void log_lock(void) { pthread_mutex_lock(&s_lock); }
static void log_unlock(void) { pthread_mutex_unlock(&s_lock); }
#endif

#define LOG_RING 400
#define LOG_LINE 256

static char s_ring[LOG_RING][LOG_LINE];
static int s_head, s_count;
static FILE *s_file;
static char s_path[1024];
static long s_bytes, s_max = 8 * 1024 * 1024;
static int s_keep = 2;
static MxLogLevel s_level = MX_LOG_INFO;

static const char *level_name(MxLogLevel l)
{
    switch (l) {
    case MX_LOG_TRACE: return "TRACE";
    case MX_LOG_INFO: return "INFO";
    case MX_LOG_WARN: return "WARN";
    case MX_LOG_ERROR: return "ERROR";
    default: return "NONE";
    }
}

static void rotate(void)
{
    if (!s_file) return;
    fclose(s_file);
    s_file = NULL;
    char from[1200], to[1200];
    if (s_keep > 0) {
        snprintf(to, sizeof(to), "%s.%d", s_path, s_keep);
        remove(to);
        for (int i = s_keep - 1; i >= 1; i--) {
            snprintf(from, sizeof(from), "%s.%d", s_path, i);
            snprintf(to, sizeof(to), "%s.%d", s_path, i + 1);
            rename(from, to);
        }
        snprintf(from, sizeof(from), "%s", s_path);
        snprintf(to, sizeof(to), "%s.1", s_path);
        rename(from, to);
    } else {
        remove(s_path);
    }
    s_file = fopen(s_path, "wb");
    s_bytes = 0;
}

static void write_line(const char *line)
{
    if (!s_file) return;
    int len = (int)strlen(line) + 1;
    if (s_max > 0 && s_bytes + len > s_max) rotate();
    if (!s_file) return;
    if (fwrite(line, 1, (size_t)(len - 1), s_file) != (size_t)(len - 1)) return;
    if (fputc('\n', s_file) == EOF) return;
    s_bytes += len;
}

void mx_log_init(const char *file, int max_bytes, int keep)
{
    log_lock();
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    s_path[0] = 0;
    s_bytes = 0;
    s_head = 0;
    s_count = 0;
    if (max_bytes > 0) s_max = max_bytes;
    s_keep = keep > 0 ? keep : 1;
    if (file && file[0]) {
        snprintf(s_path, sizeof(s_path), "%s", file);
        s_file = fopen(s_path, "ab");
        if (s_file) {
            if (fseek(s_file, 0, SEEK_END) == 0) s_bytes = ftell(s_file);
            if (s_bytes < 0) s_bytes = 0;
        }
    }
    log_unlock();
}

void mx_log_shutdown(void)
{
    log_lock();
    if (s_file) {
        fflush(s_file);
        fclose(s_file);
        s_file = NULL;
    }
    s_path[0] = 0;
    s_bytes = 0;
    s_head = 0;
    s_count = 0;
    log_unlock();
}

void mx_log_flush(void)
{
    log_lock();
    if (s_file) fflush(s_file);
    log_unlock();
}

void mx_log_set_level(MxLogLevel level) { s_level = level; }
MxLogLevel mx_log_level(void) { return s_level; }

void mx_log_v(MxLogLevel level, const char *channel, const char *fmt, va_list ap)
{
    char msg[LOG_LINE];
    char line[LOG_LINE];
    if (!fmt) return;
    if (level < s_level || level >= MX_LOG_NONE) return;
    vsnprintf(msg, sizeof(msg), fmt, ap);
    time_t now = time(NULL);
    struct tm tm_now;
#if defined(_WIN32)
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d [%s] %s: %s", tm_now.tm_year + 1900,
             tm_now.tm_mon + 1, tm_now.tm_mday, tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec,
             level_name(level), channel ? channel : "engine", msg);
    log_lock();
    snprintf(s_ring[s_head], LOG_LINE, "%s", line);
    s_head = (s_head + 1) % LOG_RING;
    if (s_count < LOG_RING) s_count++;
    write_line(line);
    log_unlock();
}

void mx_log(MxLogLevel level, const char *channel, const char *fmt, ...)
{
    va_list ap;
    if (level < s_level || level >= MX_LOG_NONE) return;
    va_start(ap, fmt);
    mx_log_v(level, channel, fmt, ap);
    va_end(ap);
}

int mx_log_count(void)
{
    log_lock();
    int n = s_count;
    log_unlock();
    return n;
}

int mx_log_lines(char *out, int out_size)
{
    int written = 0;
    if (!out || out_size <= 0) return 0;
    log_lock();
    int start = (s_head - s_count + LOG_RING) % LOG_RING;
    for (int i = 0; i < s_count; i++) {
        const char *line = s_ring[(start + i) % LOG_RING];
        int n = (int)strlen(line);
        if (written + n + 2 >= out_size) break;
        memcpy(out + written, line, (size_t)n);
        written += n;
        out[written++] = '\n';
    }
    out[written] = 0;
    log_unlock();
    return written;
}
