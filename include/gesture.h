#pragma once
#include "imu.h"

typedef enum { CAL_IDLE, CAL_STILL, CAL_LEAN_RIGHT, CAL_CENTER, CAL_BEND_FORWARD, CAL_RETURN } CalPhase;
enum { SENS_LOW, SENS_MED, SENS_HIGH };

void gesture_calibrate(bool full);
CalPhase gesture_cal_phase(void);
bool gesture_has_axes(void);
void gesture_get_axes(float axes[9]);
void gesture_set_axes(const float axes[9]);
void gesture_cancel(void);
void gesture_set_sensitivity(int level);
int gesture_update(const ImuSample *s);
void gesture_debug(float *roll, float *pitch, float *up);
