#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "game.h"

static Game g;

static void place(ObsType t, int lane)
{
    game_reset(&g, 7);
    g.next_row = 1e9f;
    g.obs[0] = (Obstacle){ t, lane, 0.1f, true };
}

static void check_rows(void)
{
    float z[MAX_OBS];
    int lanes[MAX_OBS], rows = 0;
    for (int i = 0; i < MAX_OBS; i++) {
        const Obstacle *o = &g.obs[i];
        if (!o->active || o->type == OBS_COIN) continue;
        int r = 0;
        while (r < rows && fabsf(z[r] - o->z) > 2.0f) r++;
        if (r == rows) { z[rows] = o->z; lanes[rows++] = 0; }
        lanes[r] |= 1 << o->lane;
    }
    for (int r = 0; r < rows; r++) assert(lanes[r] != (1 << LANES) - 1);
}

int main(void)
{
    game_reset(&g, 1);
    assert(g.lane == 1 && !g.over);
    assert(game_input(&g, EV_LEFT) == GF_LANE && g.lane == 0);
    assert(game_input(&g, EV_LEFT) == 0 && g.lane == 0);
    assert(game_input(&g, EV_RIGHT) == GF_LANE && game_input(&g, EV_RIGHT) == GF_LANE && g.lane == 2);
    assert(game_input(&g, EV_RIGHT) == 0);

    place(OBS_ROCK, 1);
    assert(game_update(&g, 0.016f) & GF_CRASH);
    place(OBS_ROCK, 1);
    game_input(&g, EV_JUMP);
    assert(!(game_update(&g, 0.016f) & GF_CRASH));
    place(OBS_BRANCH, 1);
    game_input(&g, EV_CROUCH);
    assert(!(game_update(&g, 0.016f) & GF_CRASH));
    place(OBS_BRANCH, 1);
    game_input(&g, EV_JUMP);
    assert(game_update(&g, 0.016f) & GF_CRASH);
    place(OBS_TREE, 1);
    game_input(&g, EV_JUMP);
    assert(game_update(&g, 0.016f) & GF_CRASH);
    assert(game_input(&g, EV_LEFT) == 0 && game_update(&g, 0.016f) == 0);
    place(OBS_TREE, 0);
    assert(!(game_update(&g, 0.016f) & GF_CRASH));
    place(OBS_COIN, 1);
    assert(game_update(&g, 0.016f) & GF_COIN && g.coins == 1 && g.score >= 50);

    game_reset(&g, 3);
    game_input(&g, EV_JUMP);
    for (int i = 0; i < 60; i++) game_update(&g, 0.016f);
    assert(g.pose == POSE_SKI);

    for (unsigned seed = 1; seed <= 100; seed++) {
        game_reset(&g, seed);
        for (int step = 0; step < 3000; step++) {
            game_update(&g, step % 500 == 499 ? 3.0f : 0.016f);
            check_rows();
            g.over = false;
        }
    }
    game_reset(&g, 5);
    for (int i = 0; i < 6000; i++) { game_update(&g, 0.016f); g.over = false; }
    assert(g.speed > 25.9f && g.speed <= 26.0f);
    puts("game tests passed");
    return 0;
}
