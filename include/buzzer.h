#pragma once
#include "common.h"

typedef enum { SND_CLICK, SND_COIN, SND_JUMP, SND_CRASH, SND_START, SND_COUNT } Sound;
bool buzzer_init(void);
void buzzer_play(Sound s);
void buzzer_close(void);
