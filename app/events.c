#include <pthread.h>
#include "events.h"

#define QN 64

const char *const ev_names[EV_COUNT] = { "LEFT", "RIGHT", "JUMP", "CROUCH", "UP", "DOWN", "SELECT", "BACK", "SCREENSHOT" };

static Event q[QN];
static int head, tail;
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

void events_init(void)
{
    pthread_mutex_lock(&m);
    head = tail = 0;
    pthread_mutex_unlock(&m);
}

void events_push(EvType type, EvSource src)
{
    pthread_mutex_lock(&m);
    int next = (head + 1) % QN;
    if (next != tail) { q[head] = (Event){ type, src, now_ms() }; head = next; }
    pthread_mutex_unlock(&m);
}

bool events_pop(Event *out)
{
    pthread_mutex_lock(&m);
    bool ok = tail != head;
    if (ok) { *out = q[tail]; tail = (tail + 1) % QN; }
    pthread_mutex_unlock(&m);
    return ok;
}
