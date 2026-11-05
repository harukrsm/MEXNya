// Exit requests and suspend/resume handling.
#ifndef MC_PLATFORM_SYSTEM_H
#define MC_PLATFORM_SYSTEM_H

// Starts the callback thread (exit + power callbacks). Call once at startup.
int sys_init(void);

// 0 once the user asked to quit (or sys_request_exit was called).
int sys_running(void);
void sys_request_exit(void);

// Returns 1 exactly once after each resume from sleep, so timers can be reset.
int sys_consume_resumed(void);

// Monotonic time in seconds.
double sys_time(void);

#endif
