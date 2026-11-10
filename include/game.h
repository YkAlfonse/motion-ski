#pragma once
#include "events.h"

#define LANES 3
#define MAX_OBS 48
#define Z_FAR 70.0f
#define JUMP_TIME 0.75f
#define CROUCH_TIME 0.85f

typedef enum { OBS_ROCK, OBS_BRANCH, OBS_TREE, OBS_COIN } ObsType;
typedef struct { ObsType type; int lane; float z; bool active; } Obstacle;
typedef enum { POSE_SKI, POSE_JUMP, POSE_CROUCH } Pose;
typedef struct {
    int lane; float lane_x; Pose pose; float pose_t;
    float speed, distance, time, next_row; int coins, score; bool over;
    Obstacle obs[MAX_OBS]; unsigned rng;
} Game;

enum { GF_LANE = 1, GF_COIN = 2, GF_CRASH = 4, GF_JUMP = 8, GF_CROUCH = 16 };
void game_reset(Game *g, unsigned seed);
int game_input(Game *g, EvType ev);
int game_update(Game *g, float dt);
