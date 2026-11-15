#include <assert.h>
#include <stdio.h>
#include "events.h"

int main(void)
{
    Event e;

    events_init();

    assert(!events_pop(&e));
    assert(!events_pop(NULL));

    uint64_t before = now_ms();

    events_push(EV_LEFT, SRC_KEYBOARD);
    events_push(EV_RIGHT, SRC_IMU);

    uint64_t after = now_ms();

    assert(!events_pop(NULL));

    assert(events_pop(&e));
    assert(e.type == EV_LEFT);
    assert(e.src == SRC_KEYBOARD);
    assert(e.t_ms >= before && e.t_ms <= after);

    assert(events_pop(&e));
    assert(e.type == EV_RIGHT);
    assert(e.src == SRC_IMU);
    assert(e.t_ms >= before && e.t_ms <= after);

    assert(!events_pop(&e));

    events_push((EvType)-1, SRC_KEYBOARD);
    events_push(EV_COUNT, SRC_KEYBOARD);
    events_push(EV_LEFT, (EvSource)-1);
    events_push(EV_LEFT, (EvSource)(SRC_IMU + 1));

    assert(!events_pop(&e));

    events_init();

    for (int i = 0; i < 70; i++)
        events_push(i % 2 == 0 ? EV_LEFT : EV_RIGHT, SRC_KEYBOARD);

    assert(events_dropped() == 7);

    for (int i = 0; i < 63; i++) {
        assert(events_pop(&e));
        assert(e.type == (i % 2 == 0 ? EV_LEFT : EV_RIGHT));
    }

    assert(!events_pop(&e));

    events_init();
    assert(events_dropped() == 0);

    for (int i = 0; i < 40; i++)
        events_push(EV_JUMP, SRC_IMU);

    for (int i = 0; i < 40; i++) {
        assert(events_pop(&e));
        assert(e.type == EV_JUMP);
    }

    for (int i = 0; i < 40; i++)
        events_push(EV_CROUCH, SRC_IMU);

    for (int i = 0; i < 40; i++) {
        assert(events_pop(&e));
        assert(e.type == EV_CROUCH);
    }

    assert(!events_pop(&e));
    assert(events_dropped() == 0);

    puts("event tests passed");
    return 0;
}
