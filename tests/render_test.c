#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "render.h"

int main(void)
{
    static const char *const items[] = { "START", "CALIBRATE", "HIGH SCORES", "SETTINGS", "QUIT" };
    static const char *const lines[] = { "SCORE 1234", "COINS 7", "NEW HIGH SCORE!", "", "SELECT TO CONTINUE" };
    const char *out = getenv("RENDER_TEST_OUT");
    char dir[] = "/tmp/render_testXXXXXX", path[512];
    Game g;
    setenv("MOTION_SKI_DISPLAY", "headless", 1);
    if (!out) out = mkdtemp(dir);
    assert(out && render_init());

    render_menu("MOTION SKI", items, 5, 0, "IMU OK  JOY OK  LED OK  BUZ OK  WEB :8088");
    snprintf(path, sizeof path, "%s/menu.ppm", out);
    assert(render_screenshot(path));

    game_reset(&g, 42);
    for (int i = 0; i < 400; i++) {
        game_update(&g, 0.016f);
        if (g.over) { g.over = false; g.lane = (g.lane + 1) % LANES; }
    }
    g.obs[0] = (Obstacle){ OBS_ROCK, 0, 6, true };
    g.obs[1] = (Obstacle){ OBS_BRANCH, 2, 12, true };
    g.obs[2] = (Obstacle){ OBS_TREE, 1, 25, true };
    g.obs[3] = (Obstacle){ OBS_COIN, 1, 4, true };
    render_game(&g, NULL);
    snprintf(path, sizeof path, "%s/game.ppm", out);
    assert(render_screenshot(path));

    game_input(&g, EV_JUMP);
    game_update(&g, JUMP_TIME / 2);
    render_game(&g, "3");
    snprintf(path, sizeof path, "%s/jump.ppm", out);
    assert(render_screenshot(path));

    render_lines("GAME OVER", lines, 5);
    snprintf(path, sizeof path, "%s/over.ppm", out);
    assert(render_screenshot(path));
    render_close();
    printf("render tests passed (%s)\n", out);
    return 0;
}
