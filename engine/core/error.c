#include "engine/core/error.h"

#include <stdarg.h>
#include <stdio.h>

#if defined(_MSC_VER)
#define MX_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define MX_THREAD_LOCAL __thread
#else
#define MX_THREAD_LOCAL
#endif

static MX_THREAD_LOCAL char s_message[512];
static MX_THREAD_LOCAL int s_code;

void mx_set_error(const char *fmt, ...)
{
    va_list ap;
    if (!fmt) {
        s_message[0] = 0;
        return;
    }
    va_start(ap, fmt);
    vsnprintf(s_message, sizeof(s_message), fmt, ap);
    va_end(ap);
}

void mx_clear_error(void)
{
    s_message[0] = 0;
    s_code = 0;
}

const char *mx_last_error(void) { return s_message; }
int mx_last_error_code(void) { return s_code; }
void mx_set_error_code(int code) { s_code = code; }
