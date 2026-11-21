#pragma once
#include "common.h"

#define MAX_SCORES 10
typedef struct { bool sound; int sensitivity; bool has_axes; float axes[9]; } Settings;

void storage_init(void);
const char *storage_dir(void);
#define MAX_USERNAME 12

typedef struct {
    int score;
    char name[MAX_USERNAME + 1];
} ScoreEntry;

int scores_get(int out[MAX_SCORES]);
int scores_add(int score);
int scores_get_named(ScoreEntry out[MAX_SCORES]);
int scores_add_named(const char *name, int score);
Settings settings_get(void);
void settings_set(const Settings *s);
