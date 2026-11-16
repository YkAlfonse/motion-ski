#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#include "common.h"
#include "events.h"
#include "gesture.h"
#include "imu_worker.h"
#include "storage.h"

#define LOST_MS 500

static pthread_t worker_thread;
static atomic_bool worker_running = ATOMIC_VAR_INIT(false);
static atomic_bool calibration_requested = ATOMIC_VAR_INIT(false);
static atomic_bool force_full = ATOMIC_VAR_INIT(false);
static atomic_int worker_status = ATOMIC_VAR_INIT(IMU_WORKER_STOPPED);
static bool worker_started = false;

// loads sensitivity and saved axes, true if the axes can be used
static bool load_saved(void)
{
    Settings s = settings_get();

    gesture_set_sensitivity(s.sensitivity);

    if (atomic_load(&force_full) || !s.has_axes)
        return false;

    // rejects axes that don't make sense
    gesture_set_axes(s.axes);
    return gesture_has_axes();
}

static void save_axes(void)
{
    Settings s = settings_get();

    s.has_axes = true;
    gesture_get_axes(s.axes);
    settings_set(&s);
}

static void *imu_worker(void *arg)
{
    (void)arg;

    if (!imu_init()) {
        atomic_store(&worker_status, IMU_WORKER_ERROR);
        return NULL;
    }

    if (!atomic_load(&worker_running)) {
        imu_close();
        return NULL;
    }

    // with saved axes only a quick calibration is needed (stand still),
    // it switches to the full one by itself if the IMU was moved a lot
    gesture_calibrate(!load_saved());
    atomic_store(&worker_status, IMU_WORKER_CALIBRATING);

    uint64_t last_ok = now_ms();
    bool lost = false;
    ImuWorkerStatus before_lost = IMU_WORKER_CALIBRATING;

    while (atomic_load(&worker_running)) {
        if (atomic_exchange(&calibration_requested, false)) {
            gesture_calibrate(true);
            atomic_store(&worker_status, IMU_WORKER_CALIBRATING);
        }

        ImuWorkerStatus status =
            (ImuWorkerStatus)atomic_load(&worker_status);

        if (status == IMU_WORKER_CALIBRATION_FAILED) {
            last_ok = now_ms();
            sleep_ms(20);
            continue;
        }

        ImuSample sample;

        if (!imu_read(&sample)) {
            // no good reading for a while during play (loose wire), show it
            // as unavailable instead of staying ready and doing nothing.
            // during calibration it stays calibrating so the game stays paused
            if (!lost && status == IMU_WORKER_READY &&
                now_ms() - last_ok > LOST_MS) {
                lost = true;
                before_lost = status;
                atomic_store(&worker_status, IMU_WORKER_ERROR);
            }
            sleep_ms(1);
            continue;
        }

        last_ok = sample.t_ms;

        // readings came back, carry on where it was
        if (lost) {
            lost = false;
            status = before_lost;
            atomic_store(&worker_status, status);
        }

        if (status == IMU_WORKER_CALIBRATING) {
            gesture_update(&sample);

            if (gesture_cal_phase() == CAL_IDLE) {
                if (gesture_has_axes()) {
                    save_axes();
                    atomic_store(&worker_status, IMU_WORKER_READY);
                } else {
                    atomic_store(&worker_status, IMU_WORKER_CALIBRATION_FAILED);
                }
            }

            continue;
        }

        if (status == IMU_WORKER_READY) {
            int event = gesture_update(&sample);

            if (event == EV_LEFT ||
                event == EV_RIGHT ||
                event == EV_JUMP ||
                event == EV_CROUCH) {
                events_push((EvType)event, SRC_IMU);
            }
        }
    }

    imu_close();

    return NULL;
}

void imu_worker_force_full(void)
{
    atomic_store(&force_full, true);
}

bool imu_worker_start(void)
{
    if (worker_started)
        return true;

    atomic_store(&worker_running, true);
    atomic_store(&calibration_requested, false);
    atomic_store(&worker_status, IMU_WORKER_STARTING);

    int result = pthread_create(
        &worker_thread, NULL, imu_worker, NULL
    );

    if (result != 0) {
        atomic_store(&worker_running, false);
        atomic_store(&worker_status, IMU_WORKER_ERROR);
        fprintf(stderr, "Failed to start IMU worker: %d\n", result);
        return false;
    }

    worker_started = true;
    return true;
}

void imu_worker_stop(void)
{
    if (!worker_started) {
        atomic_store(&worker_status, IMU_WORKER_STOPPED);
        return;
    }

    atomic_store(&worker_running, false);
    pthread_join(worker_thread, NULL);

    worker_started = false;
    atomic_store(&worker_status, IMU_WORKER_STOPPED);
}

bool imu_worker_retry_calibration(void)
{
    if (!worker_started || !atomic_load(&worker_running))
        return false;

    if (atomic_load(&worker_status) != IMU_WORKER_CALIBRATION_FAILED)
        return false;

    atomic_store(&calibration_requested, true);
    return true;
}

ImuWorkerStatus imu_worker_status(void)
{
    return (ImuWorkerStatus)atomic_load(&worker_status);
}

CalPhase imu_worker_cal_phase(void)
{
    return gesture_cal_phase();
}
