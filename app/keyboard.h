#pragma once
#include "common.h"

bool keyboard_start(void);
void keyboard_stop(void);

void keyboard_set_name_mode(bool enabled);
bool keyboard_pop_name_key(char *key);
