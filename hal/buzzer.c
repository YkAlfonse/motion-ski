#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "buzzer.h"

#define PWM_DIR "/dev/hat/pwm/GPIO12"

typedef struct {
    int hz;
    int ms;
} Note;

static const Note tunes[SND_COUNT][4] = {
    [SND_CLICK] = { { 2000, 30 } },
    [SND_COIN]  = { { 1800, 60 }, { 2400, 90 } },
    [SND_JUMP]  = { { 900, 60 }, { 1300, 60 } },
    [SND_CRASH] = { { 300, 150 }, { 200, 250 } },
    [SND_START] = { { 1000, 100 }, { 1300, 100 }, { 1600, 150 } }
};

static const char *dir;
static pthread_t th;
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static int pending = -1;
static bool quit, started;

static bool put(const char *file, long v)
{
    char path[160];
    snprintf(path, sizeof path, "%s/%s", dir, file);

    FILE *f = fopen(path, "w");
    if (!f)
        return false;

    if (fprintf(f, "%ld", v) < 0) {
        fclose(f);
        return false;
    }

    return fclose(f) == 0;
}

static void tone(int hz)
{
    if (!hz) {
        put("enable", 0);
        return;
    }

    long period = 1000000000L / hz;

    put("duty_cycle", 0);
    put("period", period);
    put("duty_cycle", period / 2);
    put("enable", 1);
}

static void *run(void *arg)
{
    (void)arg;

    pthread_mutex_lock(&m);

    while (!quit) {
        while (pending < 0 && !quit)
            pthread_cond_wait(&cv, &m);

        int s = pending;
        pending = -1;

        pthread_mutex_unlock(&m);

        for (int i = 0; s >= 0 && i < 4 && tunes[s][i].ms; i++) {
            tone(tunes[s][i].hz);
            sleep_ms(tunes[s][i].ms);
        }

        tone(0);

        pthread_mutex_lock(&m);
    }

    pthread_mutex_unlock(&m);
    return NULL;
}

bool buzzer_init(void)
{
    if (started)
        return true;

    dir = getenv("MOTION_SKI_PWM");

    if (!dir || !dir[0])
        dir = PWM_DIR;

    if (!put("duty_cycle", 0))
        return false;

    tone(0);

    pending = -1;
    quit = false;

    started = pthread_create(&th, NULL, run, NULL) == 0;
    return started;
}

void buzzer_play(Sound s)
{
    if (!started || s < 0 || s >= SND_COUNT)
        return;

    pthread_mutex_lock(&m);

    pending = s;
    pthread_cond_signal(&cv);

    pthread_mutex_unlock(&m);
}

void buzzer_close(void)
{
    if (!started)
        return;

    pthread_mutex_lock(&m);

    quit = true;
    pthread_cond_signal(&cv);

    pthread_mutex_unlock(&m);

    pthread_join(th, NULL);

    tone(0);
    started = false;
}
