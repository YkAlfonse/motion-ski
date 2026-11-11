#include <time.h>
#include <errno.h>
#include "common.h"

atomic_bool g_running = ATOMIC_VAR_INIT(true);

uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void sleep_ms(unsigned ms)
{
    struct timespec t = {
        ms / 1000,
        (long)(ms % 1000) * 1000000L
    };

    while (nanosleep(&t, &t) == -1 && errno == EINTR) {}
}
