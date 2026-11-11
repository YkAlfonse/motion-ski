#pragma once

#include <stdbool.h>

int gpio_open(const char *name, bool output, bool pull_up);
int gpio_get(int fd);
void gpio_set(int fd, bool on);
void gpio_close(int fd);