#ifndef MC_CORE_LOG_H
#define MC_CORE_LOG_H

#include <stdarg.h>

typedef enum {
    MX_LOG_TRACE = 0,
    MX_LOG_INFO = 1,
    MX_LOG_WARN = 2,
    MX_LOG_ERROR = 3,
    MX_LOG_NONE = 4
} MxLogLevel;

void mx_log_init(const char *file, int max_bytes, int keep);
void mx_log_shutdown(void);
void mx_log_flush(void);
void mx_log(MxLogLevel level, const char *channel, const char *fmt, ...);
void mx_log_v(MxLogLevel level, const char *channel, const char *fmt, va_list ap);
void mx_log_set_level(MxLogLevel level);
MxLogLevel mx_log_level(void);
int mx_log_count(void);
int mx_log_lines(char *out, int out_size);

#define MX_LOGT(channel, ...) mx_log(MX_LOG_TRACE, (channel), __VA_ARGS__)
#define MX_LOGI(channel, ...) mx_log(MX_LOG_INFO, (channel), __VA_ARGS__)
#define MX_LOGW(channel, ...) mx_log(MX_LOG_WARN, (channel), __VA_ARGS__)
#define MX_LOGE(channel, ...) mx_log(MX_LOG_ERROR, (channel), __VA_ARGS__)

#endif
