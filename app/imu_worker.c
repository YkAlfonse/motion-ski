#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#include "common.h"
#include "events.h"
#include "gesture.h"
#include "imu_worker.h"

static pthread_t worker_thread;
static atomic_bool worker_running = ATOMIC_VAR_INIT(false);
static atomic_bool calibration_requested = ATOMIC_VAR_INIT(false);
static atomic_int worker_status = ATOMIC_VAR_INIT(IMU_WORKER_STOPPED);
static bool worker_started = false;

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

    gesture_calibrate(true);
    atomic_store(&worker_status, IMU_WORKER_CALIBRATING);

    while (atomic_load(&worker_running)) {
        if (atomic_exchange(&calibration_requested, false)) {
            gesture_calibrate(true);
            atomic_store(&worker_status, IMU_WORKER_CALIBRATING);
        }

        ImuWorkerStatus status =
            (ImuWorkerStatus)atomic_load(&worker_status);

        if (status == IMU_WORKER_CALIBRATION_FAILED) {
            sleep_ms(20);
            continue;
        }

        ImuSample sample;

        if (!imu_read(&sample)) {
            sleep_ms(1);
            continue;
        }

        if (status == IMU_WORKER_CALIBRATING) {
            gesture_update(&sample);

            if (gesture_cal_phase() == CAL_IDLE) {
                if (gesture_has_axes())
                    atomic_store(&worker_status, IMU_WORKER_READY);
                else
                    atomic_store(&worker_status, IMU_WORKER_CALIBRATION_FAILED);
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
