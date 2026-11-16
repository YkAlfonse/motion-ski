#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage.h"

static char directory[512];
static bool initialized;

static bool make_directory(const char *path)
{
    char temp[512];
    size_t length = strlen(path);

    if (length >= sizeof temp)
        return false;

    strcpy(temp, path);

    for (size_t i = 1; i < length; i++) {
        if (temp[i] != '/')
            continue;

        temp[i] = '\0';

        if (mkdir(temp, 0755) < 0 && errno != EEXIST)
            return false;

        temp[i] = '/';
    }

    return mkdir(temp, 0755) == 0 || errno == EEXIST;
}

void storage_init(void)
{
    if (initialized)
        return;

    const char *custom = getenv("MOTION_SKI_DATA_DIR");
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");

    int n;

    if (custom && *custom)
        n = snprintf(directory, sizeof directory, "%s", custom);
    else if (xdg && *xdg)
        n = snprintf(directory, sizeof directory, "%s/motion-ski", xdg);
    else if (home && *home)
        n = snprintf(directory, sizeof directory,
                     "%s/.local/share/motion-ski", home);
    else
        n = snprintf(directory, sizeof directory, "./motion-ski-data");

    initialized = true;

    if (n < 0 || (size_t)n >= sizeof directory ||
        !make_directory(directory))
        fprintf(stderr, "Failed to initialize storage directory\n");
}

const char *storage_dir(void)
{
    storage_init();
    return directory;
}

static bool file_path(char *path, size_t size, const char *name)
{
    int n = snprintf(path, size, "%s/%s", storage_dir(), name);
    return n >= 0 && (size_t)n < size;
}

static int read_scores(int scores[MAX_SCORES])
{
    char path[600];
    char line[128];
    int count = 0;

    if (!file_path(path, sizeof path, "scores.txt"))
        return 0;

    FILE *file = fopen(path, "r");

    if (!file)
        return 0;

    while (fgets(line, sizeof line, file)) {
        char *end;
        errno = 0;
        long value = strtol(line, &end, 10);

        if (errno || end == line || value < 0 || value > 2147483647L)
            continue;

        int pos = 0;

        while (pos < count && scores[pos] >= value)
            pos++;

        if (pos >= MAX_SCORES)
            continue;

        if (count < MAX_SCORES)
            count++;

        for (int j = count - 1; j > pos; j--)
            scores[j] = scores[j - 1];

        scores[pos] = (int)value;
    }

    fclose(file);
    return count;
}

int scores_get(int out[MAX_SCORES])
{
    if (!out)
        return 0;

    return read_scores(out);
}

static FILE *open_temporary(const char *path, char *temp, size_t size)
{
    int n = snprintf(temp, size, "%s.tmpXXXXXX", path);

    if (n < 0 || (size_t)n >= size)
        return NULL;

    int fd = mkstemp(temp);

    if (fd < 0)
        return NULL;

    FILE *file = fdopen(fd, "w");

    if (!file) {
        close(fd);
        unlink(temp);
    }

    return file;
}

static bool save_temporary(FILE *file, const char *temp, const char *path)
{
    bool ok = fflush(file) == 0;

    if (ok)
        ok = fsync(fileno(file)) == 0;

    if (fclose(file) != 0)
        ok = false;

    if (ok)
        ok = rename(temp, path) == 0;

    if (!ok)
        unlink(temp);

    return ok;
}

int scores_add(int score)
{
    if (score < 0)
        return -1;

    int scores[MAX_SCORES];
    int count = read_scores(scores);
    int pos = 0;

    while (pos < count && scores[pos] >= score)
        pos++;

    if (pos >= MAX_SCORES)
        return -1;

    if (count < MAX_SCORES)
        count++;

    for (int i = count - 1; i > pos; i--)
        scores[i] = scores[i - 1];

    scores[pos] = score;

    char path[600], temp[640];

    if (!file_path(path, sizeof path, "scores.txt"))
        return -1;

    FILE *file = open_temporary(path, temp, sizeof temp);

    if (!file)
        return -1;

    bool ok = true;

    for (int i = 0; i < count; i++)
        if (fprintf(file, "%d\n", scores[i]) < 0)
            ok = false;

    if (!save_temporary(file, temp, path) || !ok)
        return -1;

    return pos;
}

Settings settings_get(void)
{
    Settings settings = {
        .sound = true,
        .sensitivity = 1
    };

    char path[600];

    if (!file_path(path, sizeof path, "settings.txt"))
        return settings;

    FILE *file = fopen(path, "r");

    if (!file)
        return settings;

    int sound, sensitivity, has_axes;
    float axes[9];

    if (fscanf(file, "%d %d %d", &sound, &sensitivity, &has_axes) == 3 &&
        (sound == 0 || sound == 1) &&
        sensitivity >= 0 && sensitivity <= 2 &&
        (has_axes == 0 || has_axes == 1)) {

        bool valid = true;

        for (int i = 0; i < 9; i++) {
            if (fscanf(file, "%f", &axes[i]) != 1 || !isfinite(axes[i])) {
                valid = false;
                break;
            }
        }

        if (valid) {
            settings.sound = sound;
            settings.sensitivity = sensitivity;
            settings.has_axes = has_axes;

            memcpy(settings.axes, axes, sizeof axes);
        }
    }

    fclose(file);
    return settings;
}

void settings_set(const Settings *settings)
{
    if (!settings)
        return;

    char path[600], temp[640];

    if (!file_path(path, sizeof path, "settings.txt"))
        return;

    FILE *file = open_temporary(path, temp, sizeof temp);

    if (!file)
        return;

    int sensitivity = settings->sensitivity;

    if (sensitivity < 0)
        sensitivity = 0;
    if (sensitivity > 2)
        sensitivity = 2;

    bool ok = fprintf(file, "%d %d %d\n",
                      settings->sound ? 1 : 0,
                      sensitivity,
                      settings->has_axes ? 1 : 0) >= 0;

    for (int i = 0; i < 9; i++) {
        float value = isfinite(settings->axes[i])
                          ? settings->axes[i] : 0.0f;

        if (fprintf(file, "%.9g%c", value,
                    i == 8 ? '\n' : ' ') < 0)
            ok = false;
    }

    if (!save_temporary(file, temp, path) || !ok)
        fprintf(stderr, "Failed to save settings\n");
}
