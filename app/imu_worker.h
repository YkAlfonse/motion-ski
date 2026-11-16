#pragma once

#include <stdbool.h>
#include "gesture.h"

typedef enum {
    IMU_WORKER_STOPPED,
    IMU_WORKER_STARTING,
    IMU_WORKER_CALIBRATING,
    IMU_WORKER_READY,
    IMU_WORKER_ERROR,
    IMU_WORKER_CALIBRATION_FAILED
} ImuWorkerStatus;

// call before start to ignore saved axes and do the full calibration
void imu_worker_force_full(void);
bool imu_worker_start(void);
void imu_worker_stop(void);
bool imu_worker_retry_calibration(void);
ImuWorkerStatus imu_worker_status(void);
CalPhase imu_worker_cal_phase(void);
