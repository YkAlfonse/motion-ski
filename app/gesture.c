// gesture detection from the chest IMU
#include <math.h>
#include <pthread.h>
#include <string.h>
#include "gesture.h"
#include "events.h"

#define PI_F 3.14159265f
#define RAD2DEG (180.0f / PI_F)

// calibration
#define LP_ALPHA 0.25f
// time to get into position before the still pose is measured
#define CAL_READY_MS 3000
#define CAL_STILL_N 100
#define CAL_HOLD_N 50
#define CAL_BACK_N 50
#define STILL_GYRO_DPS 10.0f
#define HOLD_GYRO_DPS 15.0f
#define STILL_ACC_TOL 0.1f
#define CAL_TILT_DEG 12.0f
// back from a hold once at least this fraction of the way out of it
#define CAL_BACK_FRAC 0.5f
// lean and bend directions can't be too close to each other
#define CAL_MAX_DOT 0.7f
// an axis shorter than this after straightening is no good
#define MIN_AXIS_LEN 0.5f
// lean or bend has to be at least ~8 deg from the final standing pose
#define MIN_HOLD_LEN 0.14f

// tilt filter
#define FILT_K 0.02f
#define FILT_ACC_TOL 0.2f
#define MAX_DT 0.05f

// thresholds at medium sensitivity
#define LEAN_ON_DEG 20.0f
#define LEAN_OFF_DEG 8.0f
#define CROUCH_ON_DEG 25.0f
#define CROUCH_OFF_DEG 10.0f
#define TILT_HOLD_N 3
#define JUMP_G 1.5f
#define JUMP_HOLD_N 2
#define LAND_TOL 0.2f
#define LAND_SETTLE_MS 150
// same as the game's jump time, also stops a landing bounce counting twice
#define JUMP_MIN_MS 700
#define JUMP_MAX_MS 1500

typedef struct {
    float x, y, z;
} vec3;

static const float sens_scale[] = { 1.3f, 1.0f, 0.75f };

// imu thread and menu both call in here
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

// learned body directions in sensor coordinates
static vec3 up, right, fwd;
static bool have_axes;
static vec3 gyro_bias;

// calibration state
static CalPhase phase = CAL_IDLE;
static bool cal_full;
static vec3 lp, sum_a, sum_g;
static bool lp_init;
static int n_cal;
static vec3 cal_up, cal_bias;
static vec3 hold_dir, lean_hold, bend_hold;
static float hold_tilt;
static uint64_t cal_t0;

// tracking state
static vec3 g_est;
static bool filt_init;
static uint64_t last_t;
static float roll_deg, pitch_deg, up_g = 1.0f;
static float scale = 1.0f;

static int lean_n, crouch_n, jump_n;
static int lean_side;
static bool crouch_down, in_air;
static uint64_t jump_t, settle_t;

static vec3 v3(float x, float y, float z)
{
    vec3 v = { x, y, z };
    return v;
}

static vec3 add(vec3 a, vec3 b)
{
    return v3(a.x + b.x, a.y + b.y, a.z + b.z);
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

static float len(vec3 a)
{
    return sqrtf(dot(a, a));
}

static vec3 unit(vec3 a)
{
    float n = len(a);
    return n > 1e-6f ? mul(a, 1.0f / n) : a;
}

// remove the part of a that points along unit vector b
static vec3 reject(vec3 a, vec3 b)
{
    return sub(a, mul(b, dot(a, b)));
}

static float angle_deg(vec3 a, vec3 b)
{
    float c = dot(unit(a), unit(b));
    if (c > 1.0f)
        c = 1.0f;
    if (c < -1.0f)
        c = -1.0f;
    return acosf(c) * RAD2DEG;
}

static void reset_tracking(void)
{
    filt_init = false;
    lean_n = crouch_n = jump_n = 0;
    lean_side = 0;
    crouch_down = in_air = false;
    roll_deg = pitch_deg = 0;
    up_g = 1.0f;
}

static void cal_start(CalPhase p)
{
    phase = p;
    n_cal = 0;
    sum_a = sum_g = v3(0, 0, 0);
}

static void cal_commit(vec3 u, vec3 r, vec3 f)
{
    up = u;
    right = r;
    fwd = f;
    gyro_bias = cal_bias;
    have_axes = true;
    phase = CAL_IDLE;
    reset_tracking();
}

// quick calibration only re-learns up, old right/forward get straightened to match
static void cal_finish_quick(void)
{
    vec3 r = reject(right, cal_up);
    vec3 f = reject(reject(fwd, cal_up), unit(r));

    // mounted too differently from last time, do the full calibration instead
    if (len(r) < MIN_AXIS_LEN || len(f) < MIN_AXIS_LEN) {
        cal_full = true;
        cal_start(CAL_LEAN_RIGHT);
        return;
    }
    cal_commit(cal_up, unit(r), unit(f));
}

// u is the final standing pose, lean and bend are measured from it
static void cal_finish_full(vec3 u)
{
    // the gravity reading moves opposite to the way the body tilts
    vec3 dr = mul(reject(sub(lean_hold, u), u), -1.0f);
    vec3 df = mul(reject(sub(bend_hold, u), u), -1.0f);

    if (len(dr) < MIN_HOLD_LEN || len(df) < MIN_HOLD_LEN ||
        fabsf(dot(unit(dr), unit(df))) > CAL_MAX_DOT) {
        // lean and bend too small or too alike, keep the old axes
        phase = CAL_IDLE;
        reset_tracking();
        return;
    }
    // split any error between right and forward evenly
    vec3 a = unit(dr), b = unit(df);
    vec3 mid = unit(add(a, b)), diff = unit(sub(a, b));
    cal_commit(u, unit(add(mid, diff)), unit(sub(mid, diff)));
}

static void cal_step(vec3 a, vec3 g, uint64_t t)
{
    bool acc_ok = fabsf(len(a) - 1.0f) < STILL_ACC_TOL;
    bool moving = len(sub(g, cal_bias)) > HOLD_GYRO_DPS;

    if (!lp_init) {
        lp = a;
        lp_init = true;
        cal_t0 = t;
    }
    lp = add(lp, mul(sub(a, lp), LP_ALPHA));

    switch (phase) {
    case CAL_STILL:
        if (t - cal_t0 < CAL_READY_MS)
            break;
        if (!acc_ok || len(g) > STILL_GYRO_DPS) {
            cal_start(CAL_STILL);
            break;
        }
        sum_a = add(sum_a, a);
        sum_g = add(sum_g, g);
        if (++n_cal >= CAL_STILL_N) {
            cal_up = unit(sum_a);
            cal_bias = mul(sum_g, 1.0f / n_cal);
            if (cal_full)
                cal_start(CAL_LEAN_RIGHT);
            else
                cal_finish_quick();
        }
        break;

    case CAL_LEAN_RIGHT:
    case CAL_BEND_FORWARD:
        // wait until leaned over and holding still, then average
        if (angle_deg(lp, cal_up) < CAL_TILT_DEG || moving) {
            n_cal = 0;
            sum_a = v3(0, 0, 0);
            break;
        }
        sum_a = add(sum_a, a);
        if (++n_cal >= CAL_HOLD_N) {
            hold_dir = unit(sum_a);
            hold_tilt = angle_deg(hold_dir, cal_up);
            if (phase == CAL_LEAN_RIGHT) {
                lean_hold = hold_dir;
                cal_start(CAL_CENTER);
            } else {
                bend_hold = hold_dir;
                cal_start(CAL_RETURN);
            }
        }
        break;

    case CAL_CENTER:
    case CAL_RETURN:
        // back means well out of the hold and holding still,
        // it doesn't have to match the starting pose exactly
        if (angle_deg(lp, hold_dir) < hold_tilt * CAL_BACK_FRAC ||
            angle_deg(lp, cal_up) > hold_tilt || moving) {
            n_cal = 0;
            sum_a = v3(0, 0, 0);
            break;
        }
        sum_a = add(sum_a, a);
        if (++n_cal >= CAL_BACK_N) {
            if (phase == CAL_CENTER)
                cal_start(CAL_BEND_FORWARD);
            else
                cal_finish_full(unit(sum_a));
        }
        break;

    default:
        break;
    }
}

// complementary filter: gyro for fast changes, accel to stop drift
static void track(const ImuSample *s, vec3 a, vec3 g)
{
    float amag = len(a);

    if (!filt_init) {
        g_est = unit(a);
        last_t = s->t_ms;
        filt_init = true;
    } else {
        float dt = (s->t_ms - last_t) / 1000.0f;
        if (dt > MAX_DT)
            dt = MAX_DT;
        last_t = s->t_ms;

        // gravity stays fixed so in the sensor frame it turns the other way
        vec3 w = mul(sub(g, gyro_bias), dt / RAD2DEG);
        g_est = unit(sub(g_est, cross(w, g_est)));

        // only trust the accel when it is close to 1 g
        if (fabsf(amag - 1.0f) < FILT_ACC_TOL)
            g_est = unit(add(mul(g_est, 1.0f - FILT_K), mul(a, FILT_K / amag)));
    }

    float gu = dot(g_est, up);
    roll_deg = atan2f(-dot(g_est, right), gu) * RAD2DEG;
    pitch_deg = atan2f(-dot(g_est, fwd), gu) * RAD2DEG;
    up_g = dot(a, g_est);
}

static int detect(uint64_t t)
{
    float lean_on = LEAN_ON_DEG * scale, lean_off = LEAN_OFF_DEG * scale;
    float crouch_on = CROUCH_ON_DEG * scale, crouch_off = CROUCH_OFF_DEG * scale;
    float jump_g = 1.0f + (JUMP_G - 1.0f) * scale;
    bool settling = false;

    // jump fires on the push off, then waits for the landing to settle
    if (!in_air) {
        jump_n = up_g > jump_g ? jump_n + 1 : 0;
        if (jump_n >= JUMP_HOLD_N) {
            in_air = true;
            jump_t = settle_t = t;
            jump_n = 0;
            lean_n = crouch_n = 0;
            return EV_JUMP;
        }
    } else {
        if (fabsf(up_g - 1.0f) > LAND_TOL)
            settle_t = t;
        settling = t - settle_t < LAND_SETTLE_MS;
        if ((t - jump_t >= JUMP_MIN_MS && !settling) || t - jump_t >= JUMP_MAX_MS)
            in_air = false;
    }

    // body wobbles in the air and on landing, ignore tilt until it settles
    if (settling) {
        lean_n = crouch_n = 0;
        return -1;
    }

    // only the bigger tilt counts, so a crouch that drifts sideways isn't a lean
    bool sideways = fabsf(roll_deg) > pitch_deg;

    // lean has to come back near center before it can fire again
    if (lean_side == 0) {
        if (sideways && roll_deg > lean_on)
            lean_n = lean_n > 0 ? lean_n + 1 : 1;
        else if (sideways && roll_deg < -lean_on)
            lean_n = lean_n < 0 ? lean_n - 1 : -1;
        else
            lean_n = 0;

        if (lean_n >= TILT_HOLD_N) {
            lean_side = 1;
            lean_n = 0;
            return EV_RIGHT;
        }
        if (lean_n <= -TILT_HOLD_N) {
            lean_side = -1;
            lean_n = 0;
            return EV_LEFT;
        }
    } else if (fabsf(roll_deg) < lean_off) {
        lean_side = 0;
    }

    if (!crouch_down) {
        // landing dips forward, and the game can't crouch mid jump anyway
        crouch_n = (!in_air && !sideways && pitch_deg > crouch_on) ? crouch_n + 1 : 0;
        if (crouch_n >= TILT_HOLD_N) {
            crouch_down = true;
            crouch_n = 0;
            return EV_CROUCH;
        }
    } else if (pitch_deg < crouch_off) {
        crouch_down = false;
    }

    return -1;
}

void gesture_calibrate(bool full)
{
    pthread_mutex_lock(&mtx);
    cal_full = full || !have_axes;
    lp_init = false;
    reset_tracking();
    cal_start(CAL_STILL);
    pthread_mutex_unlock(&mtx);
}

CalPhase gesture_cal_phase(void)
{
    pthread_mutex_lock(&mtx);
    CalPhase p = phase;
    pthread_mutex_unlock(&mtx);
    return p;
}

bool gesture_has_axes(void)
{
    pthread_mutex_lock(&mtx);
    bool h = have_axes;
    pthread_mutex_unlock(&mtx);
    return h;
}

// order is up, right, forward
void gesture_get_axes(float axes[9])
{
    pthread_mutex_lock(&mtx);
    if (!have_axes) {
        memset(axes, 0, 9 * sizeof *axes);
    } else {
        vec3 v[3] = { up, right, fwd };
        for (int i = 0; i < 3; i++) {
            axes[3 * i] = v[i].x;
            axes[3 * i + 1] = v[i].y;
            axes[3 * i + 2] = v[i].z;
        }
    }
    pthread_mutex_unlock(&mtx);
}

void gesture_set_axes(const float axes[9])
{
    vec3 u = v3(axes[0], axes[1], axes[2]);
    vec3 r = v3(axes[3], axes[4], axes[5]);
    vec3 f = v3(axes[6], axes[7], axes[8]);

    pthread_mutex_lock(&mtx);
    reset_tracking();
    have_axes = false;

    // all zeros means nothing was saved yet
    if (len(u) >= MIN_AXIS_LEN && len(r) >= MIN_AXIS_LEN && len(f) >= MIN_AXIS_LEN) {
        u = unit(u);
        r = reject(unit(r), u);
        f = reject(reject(unit(f), u), unit(r));
        // axes that point the same way shrink to almost nothing here
        if (len(r) >= MIN_AXIS_LEN && len(f) >= MIN_AXIS_LEN) {
            up = u;
            right = unit(r);
            fwd = unit(f);
            have_axes = true;
        }
    }
    pthread_mutex_unlock(&mtx);
}

void gesture_cancel(void)
{
    pthread_mutex_lock(&mtx);
    phase = CAL_IDLE;
    reset_tracking();
    pthread_mutex_unlock(&mtx);
}

void gesture_set_sensitivity(int level)
{
    if (level < SENS_LOW)
        level = SENS_LOW;
    if (level > SENS_HIGH)
        level = SENS_HIGH;
    pthread_mutex_lock(&mtx);
    scale = sens_scale[level];
    pthread_mutex_unlock(&mtx);
}

int gesture_update(const ImuSample *s)
{
    int ev = -1;

    if (!s)
        return -1;

    vec3 a = v3(s->ax, s->ay, s->az);
    vec3 g = v3(s->gx, s->gy, s->gz);

    pthread_mutex_lock(&mtx);
    if (phase != CAL_IDLE) {
        cal_step(a, g, s->t_ms);
    } else if (have_axes) {
        track(s, a, g);
        ev = detect(s->t_ms);
    }
    pthread_mutex_unlock(&mtx);
    return ev;
}

void gesture_debug(float *roll, float *pitch, float *up_out)
{
    pthread_mutex_lock(&mtx);
    if (roll)
        *roll = roll_deg;
    if (pitch)
        *pitch = pitch_deg;
    if (up_out)
        *up_out = up_g;
    pthread_mutex_unlock(&mtx);
}
