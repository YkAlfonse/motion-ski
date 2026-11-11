#include <math.h>
#include "game.h"

#define START_SPEED 14.0f
#define MAX_SPEED 26.0f
#define ACCEL 0.2f
#define ROW_GAP 18.0f
#define FIRST_ROW 20.0f
#define LANE_SLIDE 9.0f
#define COIN_POINTS 50
#define DOUBLE_AFTER_S 20.0f

static unsigned rnd(Game *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

static void add(Game *g, ObsType t, int lane, float z)
{
    for (int i = 0; i < MAX_OBS; i++)
        if (!g->obs[i].active) { g->obs[i] = (Obstacle){ t, lane, z, true }; return; }
}

static void spawn_row(Game *g, float z)
{
    int r = rnd(g) % 100, lane = rnd(g) % LANES;
    if (r < 20) {
        for (int i = 0; i < 3; i++) add(g, OBS_COIN, lane, z + i * 3.0f);
        return;
    }
    add(g, r < 45 ? OBS_ROCK : r < 70 ? OBS_BRANCH : OBS_TREE, lane, z);
    int other = (lane + 1 + rnd(g) % 2) % LANES;
    if (g->time > DOUBLE_AFTER_S && rnd(g) % 100 < 40) add(g, rnd(g) % 2 ? OBS_ROCK : OBS_TREE, other, z);
    else if (rnd(g) % 100 < 50) add(g, OBS_COIN, other, z);
}

void game_reset(Game *g, unsigned seed)
{
    *g = (Game){ .lane = 1, .lane_x = 1, .speed = START_SPEED, .next_row = FIRST_ROW, .rng = seed ? seed : 1 };
}

int game_input(Game *g, EvType ev)
{
    if (g->over) return 0;
    if (ev == EV_LEFT && g->lane > 0) { g->lane--; return GF_LANE; }
    if (ev == EV_RIGHT && g->lane < LANES - 1) { g->lane++; return GF_LANE; }
    if (ev == EV_JUMP && g->pose != POSE_JUMP) { g->pose = POSE_JUMP; g->pose_t = JUMP_TIME; return GF_JUMP; }
    if (ev == EV_CROUCH && g->pose == POSE_SKI) { g->pose = POSE_CROUCH; g->pose_t = CROUCH_TIME; return GF_CROUCH; }
    return 0;
}

int game_update(Game *g, float dt)
{
    int fl = 0;
    if (g->over) return 0;
    if (dt > 0.1f) dt = 0.1f;
    g->time += dt;
    g->speed = fminf(MAX_SPEED, START_SPEED + ACCEL * g->time);
    float dz = g->speed * dt, d = g->lane - g->lane_x, step = LANE_SLIDE * dt;
    g->distance += dz;
    g->lane_x += fabsf(d) <= step ? d : copysignf(step, d);
    if (g->pose != POSE_SKI && (g->pose_t -= dt) <= 0) g->pose = POSE_SKI;
    if ((g->next_row -= dz) <= 0) { spawn_row(g, Z_FAR); g->next_row += ROW_GAP; }
    for (int i = 0; i < MAX_OBS; i++) {
        Obstacle *o = &g->obs[i];
        if (!o->active) continue;
        float z0 = o->z;
        o->z -= dz;
        if (o->z < -6) o->active = false;
        if (!(z0 > 0 && o->z <= 0 && o->lane == g->lane)) continue;
        if (o->type == OBS_COIN) { g->coins++; o->active = false; fl |= GF_COIN; }
        else if (!(o->type == OBS_ROCK && g->pose == POSE_JUMP) && !(o->type == OBS_BRANCH && g->pose == POSE_CROUCH)) {
            g->over = true;
            fl |= GF_CRASH;
        }
    }
    g->score = (int)g->distance + COIN_POINTS * g->coins;
    return fl;
}
