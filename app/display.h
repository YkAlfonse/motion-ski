#pragma once
#include <stdbool.h>
#include <stdint.h>

bool display_open(int w, int h);
void display_present(const uint32_t *px);
void display_close(void);
