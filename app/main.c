#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "events.h"
#include "game.h"
#include "gesture.h"
#include "imu_worker.h"
#include "joystick.h"
#include "keyboard.h"
#include "storage.h"
#include "tilt_meter.h"

typedef enum {
    VIEW_MENU,
    VIEW_GAME,
    VIEW_SCORES,
    VIEW_SETTINGS
} View;

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void clear_screen(void)
{
    printf("\033[H\033[2J");
}

static int move_selection(int current, int count, int direction)
{
    return (current + count + direction) % count;
}

static const char *calibration_instruction(CalPhase phase)
{
    switch (phase) {
    case CAL_STILL:
        return "Stand still and face forward";
    case CAL_LEAN_RIGHT:
        return "Lean right and hold";
    case CAL_CENTER:
        return "Return to center";
    case CAL_BEND_FORWARD:
        return "Bend forward and hold";
    case CAL_RETURN:
        return "Stand upright";
    default:
        return "Finishing calibration";
    }
}

static void draw_menu(int selected)
{
    const char *items[] = {
        "Play Game",
        "High Scores",
        "Settings",
        "Quit"
    };

    clear_screen();

    printf("========== MOTION SKI ==========\n\n");

    for (int i = 0; i < 4; i++)
        printf(" %s %s\n", i == selected ? ">" : " ", items[i]);

    printf("\n--------------------------------\n");
    printf("W/S, I/K or Arrow Keys: Navigate\n");
    printf("ENTER: Select\n");
    printf("Q: Quit\n");

    fflush(stdout);
}

static void draw_scores(void)
{
    int scores[MAX_SCORES];
    int count = scores_get(scores);

    clear_screen();

    printf("======= MOTION SKI HIGH SCORES =======\n\n");

    if (count <= 0) {
        printf("No high scores yet.\n");
    } else {
        for (int i = 0; i < count && i < MAX_SCORES; i++)
            printf("%2d. %8d\n", i + 1, scores[i]);
    }

    printf("\nENTER or Q: Return to menu\n");

    fflush(stdout);
}

static void draw_settings(int selected, const Settings *settings)
{
    const char *levels[] = {
        "Low",
        "Medium",
        "High"
    };

    int sensitivity = settings->sensitivity;

    if (sensitivity < 0 || sensitivity > 2)
        sensitivity = 1;

    clear_screen();

    printf("========== SETTINGS ==========\n\n");

    printf(" %s Sound: %s\n",
           selected == 0 ? ">" : " ",
           settings->sound ? "ON" : "OFF");

    printf(" %s IMU Sensitivity: %s\n",
           selected == 1 ? ">" : " ",
           levels[sensitivity]);

    printf(" %s Back\n",
           selected == 2 ? ">" : " ");

    printf("\n--------------------------------\n");
    printf("W/S or Arrow Keys: Navigate\n");
    printf("A/D or Left/Right: Change\n");
    printf("ENTER: Select\n");
    printf("Q: Return to menu\n");

    fflush(stdout);
}

static void change_setting(Settings *settings, int selected,
                           int direction)
{
    if (selected == 0) {
        settings->sound = !settings->sound;
    } else if (selected == 1) {
        int sensitivity = settings->sensitivity;

        if (sensitivity < 0 || sensitivity > 2)
            sensitivity = 1;

        settings->sensitivity =
            move_selection(sensitivity, 3, direction);

        gesture_set_sensitivity(settings->sensitivity);
    } else {
        return;
    }

    settings_set(settings);
}

static void draw_game(const Game *g, ImuWorkerStatus imu_status,
                      CalPhase cal_phase)
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

        if (row < 0 || row >= 10 ||
            o->lane < 0 || o->lane >= LANES)
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

    clear_screen();

    printf("========== MOTION SKI ==========\n");
    printf("Score: %-6d Coins: %-4d Distance: %.1f\n",
           g->score, g->coins, g->distance);

    printf("Speed: %.1f  Time: %.1f\n\n",
           g->speed, g->time);

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
    printf("A/D=Steer  W=Jump  S=Crouch  Q=Menu\n");

    switch (imu_status) {
    case IMU_WORKER_STARTING:
        printf("IMU: Initializing sensor...\n");
        break;
    case IMU_WORKER_CALIBRATING:
        printf("IMU CALIBRATION: %s\n",
               calibration_instruction(cal_phase));
        break;
    case IMU_WORKER_READY:
        printf("IMU: READY - lean left/right, jump or crouch\n");
        tilt_meter_print();
        break;
    case IMU_WORKER_ERROR:
        printf("IMU: UNAVAILABLE - keyboard controls remain active\n");
        break;
    case IMU_WORKER_CALIBRATION_FAILED:
        printf("IMU: CALIBRATION FAILED - Press ENTER to retry\n");
        break;
    default:
        printf("IMU: Stopped\n");
        break;
    }

    if (g->over)
        printf("\nGAME OVER! Press ENTER to restart or Q for menu.\n");

    fflush(stdout);
}

int main(int argc, char **argv)
{
    int opt;

    // -c redoes the full IMU calibration instead of using the saved one
    while ((opt = getopt(argc, argv, "c")) != -1) {
        if (opt == 'c') {
            imu_worker_force_full();
        } else {
            fprintf(stderr, "usage: %s [-c]\n", argv[0]);
            return 1;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    events_init();
    storage_init();

    Settings settings = settings_get();
    gesture_set_sensitivity(settings.sensitivity);

    Game game;
    game_reset(&game, (unsigned)time(NULL));

    if (!keyboard_start()) {
        fprintf(stderr, "Failed to initialize keyboard\n");
        return 1;
    }

    bool joystick_available = joystick_init();

    if (!joystick_available)
        fprintf(stderr, "Joystick unavailable; keyboard controls enabled\n");

    bool imu_available = false;

    View view = VIEW_MENU;
    int menu_selection = 0;
    int settings_selection = 0;

    uint64_t previous_time = now_ms();
    uint64_t last_draw = 0;

    while (!stop_requested) {
        if (joystick_available) {
            int input = joystick_poll();

            if (input >= 0 && input < EV_COUNT)
                events_push((EvType)input, SRC_JOYSTICK);
        }

        Event event;

        while (events_pop(&event)) {
            if (view != VIEW_GAME && event.src == SRC_IMU)
                continue;

            if (event.type == EV_BACK) {
                if (view == VIEW_MENU) {
                    stop_requested = 1;
                } else {
                    if (view == VIEW_GAME) {
                        imu_worker_stop();
                        imu_available = false;
                    }

                    view = VIEW_MENU;
                }

                continue;
            }

            if (view == VIEW_MENU) {
                if (event.type == EV_UP ||
                    event.type == EV_LEFT ||
                    event.type == EV_JUMP) {
                    menu_selection =
                        move_selection(menu_selection, 4, -1);
                } else if (event.type == EV_DOWN ||
                           event.type == EV_RIGHT ||
                           event.type == EV_CROUCH) {
                    menu_selection =
                        move_selection(menu_selection, 4, 1);
                } else if (event.type == EV_SELECT) {
                    switch (menu_selection) {
                    case 0:
                        game_reset(&game, (unsigned)time(NULL));
                        imu_available = imu_worker_start();

                        if (!imu_available)
                            fprintf(stderr, "IMU unavailable; keyboard controls enabled\n");

                        view = VIEW_GAME;
                        break;
                    case 1:
                        view = VIEW_SCORES;
                        break;
                    case 2:
                        settings = settings_get();
                        settings_selection = 0;
                        view = VIEW_SETTINGS;
                        break;
                    case 3:
                        stop_requested = 1;
                        break;
                    }
                }
            } else if (view == VIEW_SCORES) {
                if (event.type == EV_SELECT)
                    view = VIEW_MENU;
            } else if (view == VIEW_SETTINGS) {
                if (event.type == EV_UP ||
                    event.type == EV_JUMP) {
                    settings_selection =
                        move_selection(settings_selection, 3, -1);
                } else if (event.type == EV_DOWN ||
                           event.type == EV_CROUCH) {
                    settings_selection =
                        move_selection(settings_selection, 3, 1);
                } else if (event.type == EV_LEFT) {
                    change_setting(&settings, settings_selection, -1);
                } else if (event.type == EV_RIGHT) {
                    change_setting(&settings, settings_selection, 1);
                } else if (event.type == EV_SELECT) {
                    if (settings_selection == 2)
                        view = VIEW_MENU;
                    else
                        change_setting(&settings, settings_selection, 1);
                }
            } else if (view == VIEW_GAME) {
                if (game.over) {
                    if (event.type == EV_SELECT) {
                        imu_worker_stop();
                        game_reset(&game, (unsigned)time(NULL));
                        imu_available = imu_worker_start();

                        if (!imu_available)
                            fprintf(stderr, "IMU unavailable; keyboard controls enabled\n");
                    }
                } else if (event.type == EV_SELECT &&
                           imu_worker_status() ==
                           IMU_WORKER_CALIBRATION_FAILED) {
                    imu_worker_retry_calibration();
                } else {
                    ImuWorkerStatus status =
                        imu_available ? imu_worker_status()
                                      : IMU_WORKER_ERROR;

                    if (status == IMU_WORKER_READY ||
                        status == IMU_WORKER_ERROR) {
                        int flags = game_input(&game, event.type);

                        if (flags & GF_CRASH)
                            scores_add(game.score);
                    }
                }
            }
        }

        if (stop_requested)
            break;

        uint64_t current_time = now_ms();

        float dt = (current_time - previous_time) / 1000.0f;
        previous_time = current_time;

        ImuWorkerStatus status =
            imu_available ? imu_worker_status()
                          : IMU_WORKER_ERROR;

        if (view == VIEW_GAME &&
            !game.over &&
            (status == IMU_WORKER_READY ||
             status == IMU_WORKER_ERROR)) {
            int flags = game_update(&game, dt);

            if (flags & GF_CRASH)
                scores_add(game.score);
        }

        if (current_time - last_draw >= 100) {
            switch (view) {
            case VIEW_MENU:
                draw_menu(menu_selection);
                break;
            case VIEW_GAME:
                draw_game(&game, status, imu_worker_cal_phase());
                break;
            case VIEW_SCORES:
                draw_scores();
                break;
            case VIEW_SETTINGS:
                draw_settings(settings_selection, &settings);
                break;
            }

            last_draw = current_time;
        }

        sleep_ms(16);
    }

    imu_worker_stop();

    if (joystick_available)
        joystick_close();

    keyboard_stop();

    printf("\nMotion Ski closed successfully.\n");
    return 0;
}
