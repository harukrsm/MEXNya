// Desktop application lifecycle and monotonic time.
#include "../system.h"
#include "../platform.h"

int sys_init(void) { return 0; }
int sys_running(void) { return plat_running(); }
void sys_request_exit(void) { plat_request_exit(); }
int sys_consume_resumed(void) { return 0; }
double sys_time(void) { return plat_time(); }
