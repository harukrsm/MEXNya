// Local wall-clock time.
#ifndef MC_PLATFORM_CLOCK_H
#define MC_PLATFORM_CLOCK_H

typedef struct {
    int year, month, day, hour, minute, second;
} ClockTime;

void clock_now(ClockTime *t);

// Debug: pretend it is this hour (-1 = the real clock).
void clock_debug_hour(int hour);

// Debug: stop the minutes and seconds at :00:00 (two photos then share a timestamp).
void clock_debug_freeze(int on);

#endif
