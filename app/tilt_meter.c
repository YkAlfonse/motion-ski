#include <stdio.h>

#include "gesture.h"
#include "tilt_meter.h"

// cell for an angle on the lean bar, -max is the left end
static int lean_cell(float deg)
{
    int i = (int)((deg + TILT_LEAN_MAX) / (2.0f * TILT_LEAN_MAX) *
                  (TILT_LEAN_W - 1) + 0.5f);
    if (i < 0)
        i = 0;
    if (i > TILT_LEAN_W - 1)
        i = TILT_LEAN_W - 1;
    return i;
}

void tilt_meter_lean(char *out, float roll, float lean_on)
{
    for (int i = 0; i < TILT_LEAN_W; i++)
        out[i] = '-';
    out[lean_cell(-lean_on)] = '|';
    out[lean_cell(lean_on)] = '|';
    out[lean_cell(roll)] = '*';
    out[TILT_LEAN_W] = '\0';
}

void tilt_meter_bend(char *out, float pitch, float crouch_on)
{
    int fill = (int)(pitch / TILT_BEND_MAX * TILT_BEND_W + 0.5f);
    if (fill < 0)
        fill = 0;
    if (fill > TILT_BEND_W)
        fill = TILT_BEND_W;

    for (int i = 0; i < TILT_BEND_W; i++)
        out[i] = i < fill ? '#' : '.';

    int mark = (int)(crouch_on / TILT_BEND_MAX * TILT_BEND_W);
    if (mark >= 0 && mark < TILT_BEND_W)
        out[mark] = '|';
    out[TILT_BEND_W] = '\0';
}

void tilt_meter_print(void)
{
    float roll, pitch, lean_on, crouch_on;
    char lean[TILT_LEAN_W + 1];
    char bend[TILT_BEND_W + 1];

    gesture_debug(&roll, &pitch, NULL);
    gesture_thresholds(&lean_on, &crouch_on);
    tilt_meter_lean(lean, roll, lean_on);
    tilt_meter_bend(bend, pitch, crouch_on);

    printf("TILT  L [%s] R  %+4.0f deg\n", lean, roll);
    printf("BEND    [%s]       %4.0f deg\n", bend, pitch);
}
