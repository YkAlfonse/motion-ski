#pragma once
#include "common.h"

typedef struct { float ax, ay, az, gx, gy, gz; uint64_t t_ms; } ImuSample;
bool imu_init(void);
bool imu_read(ImuSample *s);
void imu_close(void);
