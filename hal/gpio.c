#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include "gpio.h"

static int request(int chip, unsigned offset, bool output, bool pull_up)
{
    struct gpio_v2_line_request req = {
        .offsets = { offset },
        .num_lines = 1
    };

    strcpy(req.consumer, "motion-ski");

    req.config.flags = output
        ? GPIO_V2_LINE_FLAG_OUTPUT
        : GPIO_V2_LINE_FLAG_INPUT |
          (pull_up ? GPIO_V2_LINE_FLAG_BIAS_PULL_UP : 0);

    if (ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &req) == 0)
        return req.fd;

    // If the pull-up request failed, try without it
    if (!output && pull_up) {
        req.config.flags = GPIO_V2_LINE_FLAG_INPUT;

        if (ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &req) == 0) {
            fprintf(stderr,
                    "Warning: GPIO line %u: pull-up unavailable, using input without pull-up\n",
                    offset);
            return req.fd;
        }
    }

    return -1;
}

int gpio_open(const char *name, bool output, bool pull_up)
{
    char path[32];
    unsigned chip_n, line_n;
    if (sscanf(name, "gpiochip%u:%u", &chip_n, &line_n) == 2) {
        snprintf(path, sizeof path, "/dev/gpiochip%u", chip_n);
        int chip = open(path, O_RDWR), fd = chip < 0 ? -1 : request(chip, line_n, output, pull_up);
        if (chip >= 0) close(chip);
        return fd;
    }
    for (int c = 0; c < 16; c++) {
        struct gpiochip_info info;
        snprintf(path, sizeof path, "/dev/gpiochip%d", c);
        int chip = open(path, O_RDWR);
        if (chip < 0) continue;
        if (!ioctl(chip, GPIO_GET_CHIPINFO_IOCTL, &info))
            for (unsigned i = 0; i < info.lines; i++) {
                struct gpio_v2_line_info li = { .offset = i };
                if (ioctl(chip, GPIO_V2_GET_LINEINFO_IOCTL, &li) || strcmp(li.name, name)) continue;
                int fd = request(chip, i, output, pull_up);
                close(chip);
                return fd;
            }
        close(chip);
    }
    return -1;
}

int gpio_get(int fd)
{
    struct gpio_v2_line_values v = { .mask = 1 };
    return fd < 0 || ioctl(fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) ? -1 : (int)(v.bits & 1);
}

void gpio_set(int fd, bool on)
{
    struct gpio_v2_line_values v = { .bits = on, .mask = 1 };
    if (fd >= 0) ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

void gpio_close(int fd)
{
    if (fd >= 0) close(fd);
}
