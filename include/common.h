#pragma once
#include <stdbool.h>
#include <stdint.h>

extern volatile bool g_running;
uint64_t now_ms(void);
void sleep_ms(unsigned ms);
