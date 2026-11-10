#pragma once
#include "common.h"

bool joystick_init(void);
int joystick_poll(void);
void joystick_raw(int *x, int *y, bool *pressed);
void joystick_close(void);
