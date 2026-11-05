#ifndef MC_CORE_ERROR_H
#define MC_CORE_ERROR_H

enum {
    MX_OK = 0,
    MX_ERR_IO = -1,
    MX_ERR_PARSE = -2,
    MX_ERR_NOMEM = -3,
    MX_ERR_RANGE = -4,
    MX_ERR_STATE = -5,
    MX_ERR_NOTFOUND = -6,
    MX_ERR_EXISTS = -7,
    MX_ERR_AGAIN = -8,
    MX_ERR_UNSUPPORTED = -9,
    MX_ERR_FORMAT = -10,
    MX_ERR_DENIED = -11
};

void mx_set_error(const char *fmt, ...);
void mx_clear_error(void);
const char *mx_last_error(void);
int mx_last_error_code(void);
void mx_set_error_code(int code);

#define MX_CHECK(cond, code, ...) \
    do { \
        if (!(cond)) { \
            mx_set_error(__VA_ARGS__); \
            return (code); \
        } \
    } while (0)

#endif
