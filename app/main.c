#include <signal.h>
#include <stdio.h>
#include <time.h>

#include "common.h"
#include "events.h"
#include "game.h"
#include "keyboard.h"

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void draw_game(const Game *g)
{
    char road[10][LANES];

    for (int row = 0; row < 10; row++)
        for (int lane = 0; lane < LANES; lane++)
            road[row][lane] = ' ';

    for (int i = 0; i < MAX_OBS; i++) {
        const Obstacle *o = &g->obs[i];

        if (!o->active || o->z < 0 || o->z >= Z_FAR)
            continue;

        int row = (int)(o->z / (Z_FAR / 10.0f));

        if (row < 0 || row >= 10 || o->lane < 0 || o->lane >= LANES)
            continue;

        char symbol = ' ';

        switch (o->type) {
        case OBS_ROCK:   symbol = 'R'; break;
        case OBS_BRANCH: symbol = 'B'; break;
        case OBS_TREE:   symbol = 'T'; break;
        case OBS_COIN:   symbol = '$'; break;
        }

        road[row][o->lane] = symbol;
    }

    printf("\033[H\033[2J");

    printf("========== MOTION SKI ==========\n");
    printf("Score: %-6d Coins: %-4d Distance: %.1f\n",
           g->score, g->coins, g->distance);

    printf("Speed: %.1f  Time: %.1f\n\n", g->speed, g->time);

    for (int row = 9; row >= 0; row--)
        printf("|  %c  |  %c  |  %c  |\n",
               road[row][0], road[row][1], road[row][2]);

    printf("-----------------------\n");

    printf("|  %c  |  %c  |  %c  |\n",
           g->lane == 0 ? 'P' : ' ',
           g->lane == 1 ? 'P' : ' ',
           g->lane == 2 ? 'P' : ' ');

    const char *pose = "SKI";

    if (g->pose == POSE_JUMP)
        pose = "JUMP";
    else if (g->pose == POSE_CROUCH)
        pose = "CROUCH";

    printf("\nPlayer: %s\n", pose);
    printf("R=Rock  B=Branch  T=Tree  $=Coin\n");
    printf("A/D=Steer  W=Jump  S=Crouch  Q=Quit\n");

    if (g->over)
        printf("\nGAME OVER! Press ENTER to restart.\n");

    fflush(stdout);
}

int main(void)
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    events_init();

    Game game;
    game_reset(&game, (unsigned)time(NULL));

    if (!keyboard_start()) {
        fprintf(stderr, "Failed to initialize keyboard\n");
        return 1;
    }

    uint64_t previous_time = now_ms();
    uint64_t last_draw = 0;

    while (!stop_requested) {
        Event event;

        while (events_pop(&event)) {
            if (event.type == EV_BACK) {
                stop_requested = 1;
                break;
            }

            if (game.over) {
                if (event.type == EV_SELECT)
                    game_reset(&game, (unsigned)time(NULL));
            } else {
                game_input(&game, event.type);
            }
        }

        if (stop_requested)
            break;

        uint64_t current_time = now_ms();

        float dt = (current_time - previous_time) / 1000.0f;
        previous_time = current_time;

        if (!game.over)
            game_update(&game, dt);

        if (current_time - last_draw >= 100) {
            draw_game(&game);
            last_draw = current_time;
        }

        sleep_ms(16);
    }

    keyboard_stop();

    printf("\nMotion Ski closed successfully.\n");
    return 0;
}
