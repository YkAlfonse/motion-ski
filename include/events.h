#pragma once
#include "common.h"

typedef enum {
    EV_LEFT,
    EV_RIGHT,
    EV_JUMP,
    EV_CROUCH,
    EV_UP,
    EV_DOWN,
    EV_SELECT,
    EV_BACK,
    EV_SCREENSHOT,
    EV_COUNT
} EvType;

typedef enum {
    SRC_KEYBOARD,
    SRC_JOYSTICK,
    SRC_IMU
} EvSource;

typedef struct {
    EvType type;
    EvSource src;
    uint64_t t_ms;
} Event;

extern const char *const ev_names[EV_COUNT];

void events_init(void);
void events_push(EvType type, EvSource src);
bool events_pop(Event *out);
uint64_t events_dropped(void);
