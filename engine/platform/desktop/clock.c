// Local wall-clock time for photo names and the dawn.
#include "../clock.h"

#include <time.h>

static int s_fake_hour = -1, s_frozen;

void clock_debug_hour(int hour) { s_fake_hour = hour; }
void clock_debug_freeze(int on) { s_frozen = on; }

void clock_now(ClockTime *t)
{
    time_t now = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    t->year = tmv.tm_year + 1900;
    t->month = tmv.tm_mon + 1;
    t->day = tmv.tm_mday;
    t->hour = tmv.tm_hour;
    t->minute = tmv.tm_min;
    t->second = tmv.tm_sec;
    if (s_fake_hour >= 0)
        t->hour = s_fake_hour;
    if (s_frozen)
        t->minute = t->second = 0;
}
