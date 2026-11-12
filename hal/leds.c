#include "leds.h"
#include "gpio.h"

static const char *const pins[3] = {
    "GPIO17", "GPIO27", "GPIO22"
};

static int fd[3] = { -1, -1, -1 };

bool leds_init(void)
{
    for (int i = 0; i < 3; i++) {
        fd[i] = gpio_open(pins[i], true, false);

        if (fd[i] < 0) {
            leds_close();
            return false;
        }
    }

    leds_lane(-1);
    return true;
}

void leds_lane(int lane)
{
    for (int i = 0; i < 3; i++) {
        if (fd[i] >= 0) {
            gpio_set(fd[i], i == lane);
        }
    }
}

void leds_all(bool on)
{
    for (int i = 0; i < 3; i++) {
        if (fd[i] >= 0) {
            gpio_set(fd[i], on);
        }
    }
}

void leds_close(void)
{
    leds_all(false);

    for (int i = 0; i < 3; i++) {
        gpio_close(fd[i]);
        fd[i] = -1;
    }
}
