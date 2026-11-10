#pragma once
#include "game.h"

bool render_init(void);
void render_game(const Game *g, const char *overlay);
void render_menu(const char *title, const char *const items[], int n, int sel, const char *footer);
void render_lines(const char *title, const char *const lines[], int n);
bool render_screenshot(const char *path);
void render_close(void);
