// replays imu_log csv files through the gesture code on the PC
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gesture.h"
#include "events.h"

// only the four motion events come from gestures
#define N_GEST 4

static bool quiet;

static int parse_sens(const char *s)
{
    if (strcmp(s, "low") == 0)
        return SENS_LOW;
    if (strcmp(s, "med") == 0)
        return SENS_MED;
    if (strcmp(s, "high") == 0)
        return SENS_HIGH;
    return -1;
}

static void print_counts(const char *label, const int c[N_GEST])
{
    printf("  %-9s L %-3d R %-3d J %-3d C %-3d\n", label, c[EV_LEFT], c[EV_RIGHT], c[EV_JUMP],
           c[EV_CROUCH]);
}

// returns false if the file couldn't be replayed
static bool replay(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    float axes[9] = { 0 };
    bool saved_axes = false;
    int rec[N_GEST] = { 0 }, got[N_GEST] = { 0 };
    unsigned long rows = 0;
    unsigned long long t0 = 0, t_last = 0;

    if (!f) {
        perror(path);
        return false;
    }

    // start each file with no axes so nothing carries over
    gesture_cancel();
    gesture_set_axes(axes);

    if (!quiet)
        printf("%s\n", path);

    while (fgets(line, sizeof line, f)) {
        ImuSample s;
        unsigned long long t;
        float roll, pitch, up;
        int ev;

        if (strncmp(line, "# axes", 6) == 0) {
            float *a = axes;
            if (sscanf(line + 6, "%f %f %f %f %f %f %f %f %f", &a[0], &a[1], &a[2], &a[3],
                       &a[4], &a[5], &a[6], &a[7], &a[8]) == 9) {
                gesture_set_axes(axes);
                saved_axes = gesture_has_axes();
            }
            continue;
        }

        // skips the header and anything else that isn't a sample row
        if (sscanf(line, "%llu,%f,%f,%f,%f,%f,%f,%f,%f,%f,%d", &t, &s.ax, &s.ay, &s.az, &s.gx,
                   &s.gy, &s.gz, &roll, &pitch, &up, &ev) != 11)
            continue;
        s.t_ms = t;

        // no saved axes, so try the calibration moves at the start of the file
        if (rows == 0) {
            t0 = t;
            if (!saved_axes)
                gesture_calibrate(true);
        }
        rows++;
        t_last = t;

        if (ev >= 0 && ev < N_GEST)
            rec[ev]++;

        int e = gesture_update(&s);
        if (e >= 0 && e < N_GEST) {
            got[e]++;
            if (!quiet) {
                gesture_debug(&roll, &pitch, &up);
                printf("%9.2fs  %-7s roll %6.1f  pitch %6.1f\n", (t - t0) / 1000.0,
                       ev_names[e], roll, pitch);
            }
        }
    }
    fclose(f);

    if (rows == 0) {
        fprintf(stderr, "%s: no samples\n", path);
        return false;
    }
    if (!gesture_has_axes()) {
        fprintf(stderr,
                "%s: no calibration, the file has no '# axes' line and doesn't start with "
                "the calibration moves\n",
                path);
        return false;
    }

    printf("%s: %lu rows, %.1f s, %s\n", path, rows, (t_last - t0) / 1000.0,
           saved_axes ? "saved axes" : "calibrated from file");
    print_counts("recorded", rec);
    print_counts("replayed", got);
    return true;
}

int main(int argc, char **argv)
{
    int opt, sens = SENS_MED, bad = 0;

    while ((opt = getopt(argc, argv, "s:q")) != -1) {
        if (opt == 's' && (sens = parse_sens(optarg)) >= 0)
            continue;
        if (opt == 'q') {
            quiet = true;
            continue;
        }
        fprintf(stderr, "usage: %s [-s low|med|high] [-q] file.csv [more.csv ...]\n", argv[0]);
        return 1;
    }
    if (optind >= argc) {
        fprintf(stderr, "usage: %s [-s low|med|high] [-q] file.csv [more.csv ...]\n", argv[0]);
        return 1;
    }

    gesture_set_sensitivity(sens);
    for (int i = optind; i < argc; i++)
        if (!replay(argv[i]))
            bad++;
    return bad ? 1 : 0;
}
