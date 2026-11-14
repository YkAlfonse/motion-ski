#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include "joystick.h"
#include "events.h"
#include "gpio.h"

#define SPI_DEV "/dev/spidev0.0"
#define SPEED_HZ 250000
#define CH_X 0
#define CH_Y 1
#define BUTTON "GPIO26"
#define DEADZONE 1000
#define REPEAT_MS 350
#define INVERT_X 0
#define INVERT_Y 1

static int fd = -1, btn = -1, cx = 2048, cy = 2048, last = -1;
static uint64_t last_t;
static bool was_pressed;

static int read_ch(int ch)
{
    uint8_t tx[3] = {
        (uint8_t)(0x06 | ((ch & 4) >> 2)),
        (uint8_t)((ch & 3) << 6),
        0
    };
    uint8_t rx[3] = { 0 };

    struct spi_ioc_transfer tr = {
        .tx_buf = (uintptr_t)tx,
        .rx_buf = (uintptr_t)rx,
        .len = 3,
        .speed_hz = SPEED_HZ,
        .bits_per_word = 8
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1)
        return -1;

    return ((rx[1] & 0x0F) << 8) | rx[2];
}

bool joystick_init(void)
{
    uint8_t mode = SPI_MODE_0, bits = 8;
    uint32_t speed = SPEED_HZ;

    if ((fd = open(SPI_DEV, O_RDWR)) < 0)
        return false;

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        joystick_close();
        return false;
    }

    int x = read_ch(CH_X), y = read_ch(CH_Y);

    if (x < 0 || y < 0) {
        joystick_close();
        return false;
    }

    if (abs(x - 2048) < 600 && abs(y - 2048) < 600) {
        cx = x;
        cy = y;
    }

    btn = gpio_open(BUTTON, false, true);

    if (btn < 0) {
        joystick_close();
        return false;
    }

    last = -1;
    last_t = 0;
    was_pressed = false;

    return true;
}

void joystick_raw(int *x, int *y, bool *pressed)
{
    *x = read_ch(CH_X);
    *y = read_ch(CH_Y);
    *pressed = btn >= 0 && gpio_get(btn) == 0;
}

int joystick_poll(void)
{
    int x, y;
    bool p;

    joystick_raw(&x, &y, &p);

    if (p && !was_pressed) {
        was_pressed = true;
        return EV_SELECT;
    }

    was_pressed = p;

    if (x < 0 || y < 0)
        return -1;

    int dx = (x - cx) * (INVERT_X ? -1 : 1);
    int dy = (y - cy) * (INVERT_Y ? -1 : 1);
    int dir = -1;

    if (abs(dx) > DEADZONE && abs(dx) >= abs(dy))
        dir = dx < 0 ? EV_LEFT : EV_RIGHT;
    else if (abs(dy) > DEADZONE)
        dir = dy > 0 ? EV_UP : EV_DOWN;

    uint64_t t = now_ms();

    if (dir < 0) {
        last = -1;
        return -1;
    }

    if (dir != last || t - last_t >= REPEAT_MS) {
        last = dir;
        last_t = t;
        return dir;
    }

    return -1;
}

void joystick_close(void)
{
    gpio_close(btn);

    if (fd >= 0)
        close(fd);

    fd = btn = -1;
}
