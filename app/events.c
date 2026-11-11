#include <pthread.h>
#include "events.h"

#define QN 64

const char *const ev_names[EV_COUNT] = {
    "LEFT", "RIGHT", "JUMP", "CROUCH",
    "UP", "DOWN", "SELECT", "BACK", "SCREENSHOT"
};

static Event q[QN];
static int head = 0;
static int tail = 0;
static uint64_t dropped = 0;
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

void events_init(void)
{
    pthread_mutex_lock(&m);
    head = 0;
    tail = 0;
    dropped = 0;
    pthread_mutex_unlock(&m);
}

void events_push(EvType type, EvSource src)
{
    if ((unsigned)type >= EV_COUNT)
        return;

    if ((unsigned)src > SRC_IMU)
        return;

    pthread_mutex_lock(&m);

    int next = (head + 1) % QN;

    if (next != tail) {
        q[head] = (Event){ type, src, now_ms() };
        head = next;
    } else {
        dropped++;
    }

    pthread_mutex_unlock(&m);
}

bool events_pop(Event *out)
{
    if (!out)
        return false;

    pthread_mutex_lock(&m);

    if (tail == head) {
        pthread_mutex_unlock(&m);
        return false;
    }

    *out = q[tail];
    tail = (tail + 1) % QN;

    pthread_mutex_unlock(&m);
    return true;
}

uint64_t events_dropped(void)
{
    pthread_mutex_lock(&m);
    uint64_t count = dropped;
    pthread_mutex_unlock(&m);
    return count;
}
