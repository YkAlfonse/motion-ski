// gesture tests with simulated IMU samples, no board needed
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "gesture.h"
#include "events.h"

#define PI_F 3.14159265f
#define STEP_MS 10

typedef struct {
    float x, y, z;
} vec3;

// body directions in sensor coordinates, tilted on purpose
static const vec3 U = { 0.0f, 0.6f, 0.8f };
static const vec3 R = { 1.0f, 0.0f, 0.0f };
static const vec3 F = { 0.0f, 0.8f, -0.6f };
static const vec3 BIAS = { 1.5f, -0.8f, 0.5f };

static float cur_roll, cur_pitch;
static vec3 prev_g;
static bool have_prev;
static uint64_t t = 1000;
static unsigned seed = 1;
static int counts[EV_COUNT];

static vec3 v3(float x, float y, float z)
{
    vec3 v = { x, y, z };
    return v;
}

static vec3 sub(vec3 a, vec3 b)
{
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static vec3 mul(vec3 a, float k)
{
    return v3(a.x * k, a.y * k, a.z * k);
}

static float dot(vec3 a, vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static vec3 cross(vec3 a, vec3 b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static vec3 unit(vec3 a)
{
    return mul(a, 1.0f / sqrtf(dot(a, a)));
}

// small repeatable noise in [-amp, amp]
static float noise(float amp)
{
    seed = seed * 1103515245u + 12345u;
    return amp * (((seed >> 16) & 0x7fff) / 16383.5f - 1.0f);
}

// direction of world up seen by the sensor for a body lean in degrees
static vec3 world_up(float roll, float pitch)
{
    float tr = tanf(roll * PI_F / 180.0f), tp = tanf(pitch * PI_F / 180.0f);
    return unit(sub(sub(U, mul(R, tr)), mul(F, tp)));
}

// one sample at the current pose, extra is vertical accel in g
static void step(float extra)
{
    vec3 g = world_up(cur_roll, cur_pitch);
    vec3 w = v3(0, 0, 0);

    // gyro rate that turns the last gravity direction into this one
    if (have_prev)
        w = mul(cross(sub(g, prev_g), g), 1000.0f / STEP_MS * 180.0f / PI_F);
    prev_g = g;
    have_prev = true;

    vec3 a = mul(g, 1.0f + extra);
    ImuSample s = {
        a.x + noise(0.01f), a.y + noise(0.01f), a.z + noise(0.01f),
        w.x + BIAS.x + noise(0.5f), w.y + BIAS.y + noise(0.5f), w.z + BIAS.z + noise(0.5f),
        t,
    };
    t += STEP_MS;

    int ev = gesture_update(&s);
    assert(ev >= -1 && ev < EV_COUNT);
    if (ev >= 0)
        counts[ev]++;
}

static void hold(float secs, float extra)
{
    int n = (int)(secs * 1000 / STEP_MS);
    for (int i = 0; i < n; i++)
        step(extra);
}

static void move_to(float roll, float pitch, float secs)
{
    int n = (int)(secs * 1000 / STEP_MS);
    float r0 = cur_roll, p0 = cur_pitch;
    for (int i = 1; i <= n; i++) {
        cur_roll = r0 + (roll - r0) * i / n;
        cur_pitch = p0 + (pitch - p0) * i / n;
        step(0);
    }
}

static void lean_and_back(float roll, float pitch)
{
    move_to(roll, pitch, 0.3f);
    hold(0.5f, 0);
    move_to(0, 0, 0.3f);
    hold(0.5f, 0);
}

static void jump(void)
{
    hold(0.15f, 1.0f);
    hold(0.35f, -0.95f);
    hold(0.1f, 1.5f);
    hold(1.0f, 0);
}

static void expect(const char *name, int left, int right, int jmp, int crouch)
{
    printf("%-26s L%d R%d J%d C%d\n", name, counts[EV_LEFT], counts[EV_RIGHT],
           counts[EV_JUMP], counts[EV_CROUCH]);
    assert(counts[EV_LEFT] == left);
    assert(counts[EV_RIGHT] == right);
    assert(counts[EV_JUMP] == jmp);
    assert(counts[EV_CROUCH] == crouch);
    for (int i = 0; i < EV_COUNT; i++)
        counts[i] = 0;
}

static float axis_match(const float *axes, int i, vec3 want)
{
    return dot(v3(axes[3 * i], axes[3 * i + 1], axes[3 * i + 2]), want);
}

int main(void)
{
    float axes[9], saved[9];

    assert(gesture_update(NULL) == -1);

    // full calibration, same steps the player does
    gesture_calibrate(true);
    assert(gesture_cal_phase() == CAL_STILL);
    hold(1.5f, 0);
    assert(gesture_cal_phase() == CAL_LEAN_RIGHT);
    move_to(25, 0, 0.5f);
    hold(1.0f, 0);
    assert(gesture_cal_phase() == CAL_CENTER);
    move_to(0, 0, 0.5f);
    hold(0.6f, 0);
    assert(gesture_cal_phase() == CAL_BEND_FORWARD);
    move_to(0, 35, 0.5f);
    hold(1.0f, 0);
    assert(gesture_cal_phase() == CAL_RETURN);
    move_to(0, 0, 0.5f);
    hold(0.6f, 0);
    assert(gesture_cal_phase() == CAL_IDLE);
    assert(gesture_has_axes());

    gesture_get_axes(axes);
    printf("axes match up %.3f right %.3f fwd %.3f\n", axis_match(axes, 0, U),
           axis_match(axes, 1, R), axis_match(axes, 2, F));
    assert(axis_match(axes, 0, U) > 0.99f);
    assert(axis_match(axes, 1, R) > 0.98f);
    assert(axis_match(axes, 2, F) > 0.98f);
    expect("calibration", 0, 0, 0, 0);

    hold(3.0f, 0);
    expect("standing still", 0, 0, 0, 0);

    lean_and_back(25, 0);
    expect("lean right", 0, 1, 0, 0);

    lean_and_back(-25, 0);
    expect("lean left", 1, 0, 0, 0);

    lean_and_back(0, 35);
    expect("bend forward", 0, 0, 0, 1);

    // holding the lean should not repeat
    move_to(25, 0, 0.3f);
    hold(2.0f, 0);
    move_to(0, 0, 0.3f);
    hold(0.5f, 0);
    expect("long lean right", 0, 1, 0, 0);

    jump();
    expect("jump", 0, 0, 1, 0);

    jump();
    jump();
    expect("two jumps", 0, 0, 2, 0);

    lean_and_back(10, 0);
    lean_and_back(-10, 0);
    lean_and_back(0, 15);
    expect("small sway", 0, 0, 0, 0);

    gesture_set_sensitivity(SENS_HIGH);
    lean_and_back(13, 0);
    expect("13 deg lean, high sens", 0, 1, 0, 0);

    gesture_set_sensitivity(SENS_MED);
    lean_and_back(13, 0);
    expect("13 deg lean, med sens", 0, 0, 0, 0);

    // quick calibration keeps the old right and forward
    gesture_get_axes(saved);
    gesture_calibrate(false);
    hold(1.5f, 0);
    assert(gesture_cal_phase() == CAL_IDLE);
    gesture_get_axes(axes);
    assert(axis_match(axes, 1, R) > 0.98f);
    assert(axis_match(axes, 2, F) > 0.98f);
    expect("quick calibration", 0, 0, 0, 0);

    // cancel keeps the axes we had
    gesture_calibrate(true);
    hold(0.5f, 0);
    gesture_cancel();
    assert(gesture_cal_phase() == CAL_IDLE);
    assert(gesture_has_axes());

    // nothing saved yet, so no gestures
    float zero[9] = { 0 };
    gesture_set_axes(zero);
    assert(!gesture_has_axes());
    lean_and_back(25, 0);
    expect("no axes", 0, 0, 0, 0);

    // loading saved axes works like calibrating
    gesture_set_axes(saved);
    assert(gesture_has_axes());
    lean_and_back(25, 0);
    jump();
    expect("saved axes", 0, 1, 1, 0);

    printf("all gesture tests passed\n");
    return 0;
}
