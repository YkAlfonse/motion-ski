#pragma once
#include "common.h"

#define MAX_SCORES 10
typedef struct { bool sound; int sensitivity; bool has_axes; float axes[9]; } Settings;

void storage_init(void);
const char *storage_dir(void);
int scores_get(int out[MAX_SCORES]);
int scores_add(int score);
Settings settings_get(void);
void settings_set(const Settings *s);
