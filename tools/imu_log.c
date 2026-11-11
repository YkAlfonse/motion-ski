// records IMU samples as CSV on stdout, summary on stderr
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "imu.h"
#include "gesture.h"
#include "events.h"

static const char *const cal_msg[] = {
    [CAL_IDLE] = "",
    [CAL_STILL] = "stand still and face forward",
    [CAL_LEAN_RIGHT] = "lean right and hold",
    [CAL_CENTER] = "stand up straight",
    [CAL_BEND_FORWARD] = "bend forward and hold",
    [CAL_RETURN] = "stand up straight",
};

static void on_sigint(int sig)
{
    (void)sig;
    g_running = false;
}

// guided calibration, returns true if axes were learned
static bool run_calibration(void)
{
    ImuSample s;
    CalPhase last = CAL_IDLE;

    gesture_calibrate(true);
    while (g_running && gesture_cal_phase() != CAL_IDLE) {
        CalPhase p = gesture_cal_phase();
        if (p != last) {
            fprintf(stderr, ">> %s\n", cal_msg[p]);
            last = p;
        }
        if (imu_read(&s))
            gesture_update(&s);
        else
            sleep_ms(5);
    }
    return g_running && gesture_has_axes();
}

int main(int argc, char **argv)
{
    bool guided = false;
    double secs = 0;
    int opt;

    while ((opt = getopt(argc, argv, "gt:")) != -1) {
        if (opt == 'g') {
            guided = true;
        } else if (opt == 't') {
            secs = atof(optarg);
        } else {
            fprintf(stderr, "usage: %s [-g] [-t seconds] > file.csv\n", argv[0]);
            return 1;
        }
    }

    signal(SIGINT, on_sigint);
    if (!imu_init())
        return 1;
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (guided) {
        if (run_calibration()) {
            float axes[9];
            gesture_get_axes(axes);
            printf("# axes");
            for (int i = 0; i < 9; i++)
                printf(" %.5f", axes[i]);
            printf("\n");
            fprintf(stderr, ">> calibrated, recording (ctrl+c to stop)\n");
        } else if (g_running) {
            fprintf(stderr, ">> no calibration, recording raw only\n");
            guided = false;
        }
    }

    printf("t_ms,ax,ay,az,gx,gy,gz,roll,pitch,up,ev\n");

    uint64_t start = now_ms(), first = 0, last = 0;
    unsigned long n = 0, fails = 0;

    while (g_running) {
        ImuSample s;
        float roll = 0, pitch = 0, up = 0;
        int ev = -1;

        if (secs > 0 && now_ms() - start >= secs * 1000)
            break;

        if (!imu_read(&s)) {
            fails++;
            sleep_ms(5);
            continue;
        }
        if (guided) {
            ev = gesture_update(&s);
            gesture_debug(&roll, &pitch, &up);
        }

        printf("%llu,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.4f,%d\n", (unsigned long long)s.t_ms,
               s.ax, s.ay, s.az, s.gx, s.gy, s.gz, roll, pitch, up, ev);
        if (ev >= 0 && ev < EV_COUNT)
            fprintf(stderr, "%s\n", ev_names[ev]);

        if (n == 0)
            first = s.t_ms;
        last = s.t_ms;
        n++;
    }

    imu_close();

    double span = (last - first) / 1000.0;
    fprintf(stderr, "%lu samples in %.1f s (%.1f Hz), %lu failed reads\n", n, span,
            span > 0 ? (n - 1) / span : 0.0, fails);
    return 0;
}
