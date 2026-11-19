#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gesture.h"
#include "tilt_meter.h"

static int count(const char *s, char c)
{
    int n = 0;
    for (; *s; s++)
        n += *s == c;
    return n;
}

static int find(const char *s, char c)
{
    const char *p = strchr(s, c);
    return p ? (int)(p - s) : -1;
}

static void test_lean_centered(void)
{
    char bar[TILT_LEAN_W + 1];
    tilt_meter_lean(bar, 0.0f, 20.0f);

    assert(strlen(bar) == TILT_LEAN_W);
    assert(bar[TILT_LEAN_W / 2] == '*');
    assert(count(bar, '|') == 2);
    // marks are the same distance from the middle
    int left = find(bar, '|');
    int right = (int)(strrchr(bar, '|') - bar);
    assert(TILT_LEAN_W / 2 - left == right - TILT_LEAN_W / 2);
    puts("  lean centered");
}

static void test_lean_past_line(void)
{
    char bar[TILT_LEAN_W + 1];

    tilt_meter_lean(bar, 30.0f, 20.0f);
    assert(find(bar, '*') > (int)(strrchr(bar, '|') - bar));

    tilt_meter_lean(bar, -30.0f, 20.0f);
    assert(find(bar, '*') < find(bar, '|'));

    // under the line stays inside the marks
    tilt_meter_lean(bar, 10.0f, 20.0f);
    assert(find(bar, '*') > find(bar, '|'));
    assert(find(bar, '*') < (int)(strrchr(bar, '|') - bar));
    puts("  lean past line");
}

static void test_lean_clamped(void)
{
    char bar[TILT_LEAN_W + 1];

    tilt_meter_lean(bar, 90.0f, 20.0f);
    assert(bar[TILT_LEAN_W - 1] == '*');
    assert(strlen(bar) == TILT_LEAN_W);

    tilt_meter_lean(bar, -90.0f, 20.0f);
    assert(bar[0] == '*');
    puts("  lean clamped");
}

static void test_bend_fill(void)
{
    char bar[TILT_BEND_W + 1];

    tilt_meter_bend(bar, 0.0f, 25.0f);
    assert(strlen(bar) == TILT_BEND_W);
    assert(count(bar, '#') == 0);
    assert(count(bar, '|') == 1);

    // leaning back shows nothing
    tilt_meter_bend(bar, -15.0f, 25.0f);
    assert(count(bar, '#') == 0);

    // half way fills half
    tilt_meter_bend(bar, TILT_BEND_MAX / 2, 25.0f);
    assert(count(bar, '#') == TILT_BEND_W / 2);

    tilt_meter_bend(bar, 80.0f, 25.0f);
    assert(count(bar, '.') == 0);
    puts("  bend fill");
}

static void test_bend_line(void)
{
    char bar[TILT_BEND_W + 1];

    // just under the crouch angle doesn't reach the line
    tilt_meter_bend(bar, 24.0f, 25.0f);
    int mark = find(bar, '|');
    assert(mark > 0 && bar[mark - 1] == '#' && bar[mark + 1] == '.');
    assert(count(bar, '#') < mark + 1);

    tilt_meter_bend(bar, 30.0f, 25.0f);
    assert(bar[find(bar, '|') + 1] == '#');
    puts("  bend line");
}

static void test_thresholds(void)
{
    float lean, crouch;

    gesture_set_sensitivity(SENS_MED);
    gesture_thresholds(&lean, &crouch);
    assert(fabsf(lean - 20.0f) < 0.01f && fabsf(crouch - 25.0f) < 0.01f);

    gesture_set_sensitivity(SENS_HIGH);
    gesture_thresholds(&lean, &crouch);
    assert(fabsf(lean - 15.0f) < 0.01f && fabsf(crouch - 18.75f) < 0.01f);

    gesture_set_sensitivity(SENS_LOW);
    gesture_thresholds(&lean, &crouch);
    assert(fabsf(lean - 26.0f) < 0.01f && fabsf(crouch - 32.5f) < 0.01f);

    // marks move out on low sensitivity
    char med[TILT_LEAN_W + 1], low[TILT_LEAN_W + 1];
    tilt_meter_lean(med, 0.0f, 20.0f);
    tilt_meter_lean(low, 0.0f, 26.0f);
    assert(find(low, '|') < find(med, '|'));

    gesture_thresholds(NULL, NULL);
    gesture_set_sensitivity(SENS_MED);
    puts("  thresholds");
}

int main(void)
{
    test_lean_centered();
    test_lean_past_line();
    test_lean_clamped();
    test_bend_fill();
    test_bend_line();
    test_thresholds();
    puts("tilt meter tests passed");
    return 0;
}
