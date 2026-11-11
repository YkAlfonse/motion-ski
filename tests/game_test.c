#include <assert.h>
#include <stdio.h>
#include "game.h"

static Game g;

static void place(ObsType t, int lane)
{
    game_reset(&g, 7);
    g.next_row = 1e9f;
    g.obs[0] = (Obstacle){ t, lane, 0.1f, true };
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

    for (unsigned seed = 1; seed < 200; seed++) {
        game_reset(&g, seed);
        for (int step = 0; step < 6000 && !g.over; step++) {
            g.lane = 1;
            game_update(&g, 0.016f);
            for (int i = 0; i < MAX_OBS; i++) {
                int blocked = 0;
                for (int j = 0; j < MAX_OBS; j++)
                    blocked += g.obs[i].active && g.obs[j].active && g.obs[j].type != OBS_COIN && g.obs[j].z == g.obs[i].z;
                assert(blocked < LANES);
            }
            if (g.over) game_reset(&g, seed + 1000);
        }
    }
    game_reset(&g, 5);
    for (int i = 0; i < 6000; i++) { game_update(&g, 0.016f); g.over = false; }
    assert(g.speed > 25.9f && g.speed <= 26.0f);
    puts("game tests passed");
    return 0;
}
