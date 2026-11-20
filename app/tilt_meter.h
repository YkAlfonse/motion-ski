#pragma once

#define TILT_LEAN_W 25
#define TILT_BEND_W 20
#define TILT_LEAN_MAX 40.0f
#define TILT_BEND_MAX 40.0f

// out needs TILT_LEAN_W + 1 chars. '*' is the roll, '|' the lean angles
void tilt_meter_lean(char *out, float roll, float lean_on);
// out needs TILT_BEND_W + 1 chars. '#' fills with pitch, '|' the crouch angle
void tilt_meter_bend(char *out, float pitch, float crouch_on);
// prints both bars using the current gesture values
void tilt_meter_print(void);
