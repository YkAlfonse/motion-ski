#include <stdio.h>
#include <string.h>
#include "render.h"
#include "gesture.h"
#include "joystick.h"
#include "leds.h"
#include "buzzer.h"
#include "web.h"
#include "storage.h"
#include "keyboard.h"

#define WEAK __attribute__((weak))

static void say(const char *what)
{
    static char last[96];
    if (strcmp(last, what)) { snprintf(last, sizeof last, "%s", what); printf("[stub screen] %s\n", what); }
}

WEAK bool imu_init(void) { return false; }
WEAK bool imu_read(ImuSample *s) { (void)s; return false; }
WEAK void imu_close(void) {}

WEAK void gesture_calibrate(bool full) { (void)full; }
WEAK CalPhase gesture_cal_phase(void) { return CAL_IDLE; }
WEAK bool gesture_has_axes(void) { return false; }
WEAK void gesture_get_axes(float axes[9]) { memset(axes, 0, 9 * sizeof *axes); }
WEAK void gesture_set_axes(const float axes[9]) { (void)axes; }
WEAK void gesture_cancel(void) {}
WEAK void gesture_set_sensitivity(int level) { (void)level; }
WEAK int gesture_update(const ImuSample *s) { (void)s; return -1; }
WEAK void gesture_debug(float *roll, float *pitch, float *up) { *roll = *pitch = 0; *up = 1; }

WEAK bool joystick_init(void) { return false; }
WEAK int joystick_poll(void) { return -1; }
WEAK void joystick_raw(int *x, int *y, bool *pressed) { *x = *y = 2048; *pressed = false; }
WEAK void joystick_close(void) {}

WEAK bool leds_init(void) { return false; }
WEAK void leds_lane(int lane) { (void)lane; }
WEAK void leds_all(bool on) { (void)on; }
WEAK void leds_close(void) {}

WEAK bool buzzer_init(void) { return false; }
WEAK void buzzer_play(Sound s) { (void)s; }
WEAK void buzzer_close(void) {}

WEAK void storage_init(void) {}
WEAK const char *storage_dir(void) { return "."; }
WEAK int scores_get(int out[MAX_SCORES]) { (void)out; return 0; }
WEAK int scores_add(int score) { (void)score; return -1; }
WEAK Settings settings_get(void) { return (Settings){ .sound = true, .sensitivity = 1 }; }
WEAK void settings_set(const Settings *s) { (void)s; }

WEAK bool keyboard_start(void) { return false; }
WEAK void keyboard_stop(void) {}

WEAK bool web_start(int port) { (void)port; return false; }
WEAK void web_stop(void) {}

WEAK void game_reset(Game *g, unsigned seed) { (void)seed; *g = (Game){ .lane = 1, .lane_x = 1 }; }
WEAK int game_input(Game *g, EvType ev) { (void)g; (void)ev; return 0; }
WEAK int game_update(Game *g, float dt) { g->time += dt; return 0; }

WEAK bool render_init(void) { puts("[stub screen] no renderer yet, printing screens instead"); return true; }
WEAK void render_game(const Game *g, const char *overlay) { (void)g; say(overlay ? overlay : "game running"); }
WEAK void render_menu(const char *title, const char *const items[], int n, int sel, const char *footer)
{
    char b[96];
    (void)n; (void)footer;
    snprintf(b, sizeof b, "%s > %s", title, items[sel]);
    say(b);
}
WEAK void render_lines(const char *title, const char *const lines[], int n) { say(n ? lines[0] : title); }
WEAK bool render_screenshot(const char *path) { (void)path; return false; }
WEAK void render_close(void) {}
