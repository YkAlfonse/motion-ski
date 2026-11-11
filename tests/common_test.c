#include <assert.h>
#include <stdio.h>
#include "common.h"

int main(void)
{
    atomic_store(&g_running, true);
    assert(atomic_load(&g_running));

    uint64_t start = now_ms();
    sleep_ms(20);
    uint64_t elapsed = now_ms() - start;

    assert(elapsed >= 15);

    atomic_store(&g_running, false);
    assert(!atomic_load(&g_running));

    atomic_store(&g_running, true);

    puts("common tests passed");
    return 0;
}
