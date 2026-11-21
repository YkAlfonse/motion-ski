#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "storage.h"

int main(void)
{
    char dir[] = "/tmp/motion-ski-storage-XXXXXX";

    assert(mkdtemp(dir) != NULL);
    assert(setenv("MOTION_SKI_DATA_DIR", dir, 1) == 0);

    storage_init();

    Settings defaults = settings_get();
    assert(defaults.sound);
    assert(defaults.sensitivity == 1);
    assert(!defaults.has_axes);

    int scores[MAX_SCORES];

    assert(scores_get(scores) == 0);

    for (int i = 0; i < 12; i++)
        assert(scores_add(i * 10) == 0);

    assert(scores_get(scores) == MAX_SCORES);

    for (int i = 0; i < MAX_SCORES; i++)
        assert(scores[i] == 110 - i * 10);

    assert(scores_add(42) == 7);
    assert(scores_add(0) == -1);
    assert(scores_add(-5) == -1);

    assert(scores_get(scores) == MAX_SCORES);
    assert(scores[7] == 42);

    ScoreEntry named[MAX_SCORES];

    assert(scores_get_named(named) == MAX_SCORES);
    assert(named[0].score == 110);
    assert(strcmp(named[0].name, "PLAYER") == 0);

    assert(scores_add_named("Alfonse", 150) == 0);
    assert(scores_add_named("Sehaj", 125) == 1);

    assert(scores_get_named(named) == MAX_SCORES);
    assert(named[0].score == 150);
    assert(strcmp(named[0].name, "ALFONSE") == 0);
    assert(named[1].score == 125);
    assert(strcmp(named[1].name, "SEHAJ") == 0);
    assert(named[2].score == 110);
    assert(strcmp(named[2].name, "PLAYER") == 0);

    assert(scores_add_named("", 200) == -1);
    assert(scores_add_named("BAD NAME", 200) == -1);
    assert(scores_add_named("TOOLONGUSERNAME", 200) == -1);
    assert(scores_add_named("ALFONSE", -1) == -1);

    assert(scores_get_named(named) == MAX_SCORES);
    assert(named[0].score == 150);

    Settings saved = {
        .sound = false,
        .sensitivity = 2,
        .has_axes = true,
        .axes = { 0, 0, 1, 1, 0, 0, 0, 1, 0 }
    };

    settings_set(&saved);

    Settings loaded = settings_get();

    assert(!loaded.sound);
    assert(loaded.sensitivity == 2);
    assert(loaded.has_axes);

    for (int i = 0; i < 9; i++)
        assert(loaded.axes[i] == saved.axes[i]);

    char path[600];

    snprintf(path, sizeof path, "%s/scores.txt", dir);
    assert(access(path, F_OK) == 0);
    assert(unlink(path) == 0);

    snprintf(path, sizeof path, "%s/settings.txt", dir);
    assert(access(path, F_OK) == 0);
    assert(unlink(path) == 0);

    assert(rmdir(dir) == 0);

    puts("Storage tests passed");
    return 0;
}
