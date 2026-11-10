// LSM6DSOX accel + gyro over I2C
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include "imu.h"

#define IMU_BUS "/dev/i2c-1"
#define ADDR_A 0x6A
#define ADDR_B 0x6B

#define REG_WHO_AM_I 0x0F
#define REG_CTRL1_XL 0x10
#define REG_CTRL2_G 0x11
#define REG_CTRL3_C 0x12
#define REG_STATUS 0x1E
#define REG_OUTX_L_G 0x22

#define CHIP_ID 0x6C

// 104 Hz, +-4 g
#define CTRL1_VAL 0x48
// 104 Hz, 500 dps
#define CTRL2_VAL 0x44
// block data update + address auto increment
#define CTRL3_VAL 0x44
#define CTRL3_RESET 0x01

#define STATUS_XLDA 0x01
#define STATUS_GDA 0x02
#define STATUS_BOTH (STATUS_XLDA | STATUS_GDA)

// datasheet sensitivity for the ranges above
#define ACC_G_PER_LSB 0.000122f
#define GYRO_DPS_PER_LSB 0.0175f

// max wait for new data, 1 ms per try (one sample is ~9.6 ms)
#define WAIT_TRIES 20

static int fd = -1;
static uint16_t addr;

static bool reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    struct i2c_msg msg = { .addr = addr, .flags = 0, .len = 2, .buf = buf };
    struct i2c_rdwr_ioctl_data xfer = { .msgs = &msg, .nmsgs = 1 };
    return ioctl(fd, I2C_RDWR, &xfer) == 1;
}

// write the register address then read n bytes (repeated start)
static bool reg_read(uint8_t reg, uint8_t *buf, uint16_t n)
{
    struct i2c_msg msgs[2] = {
        { .addr = addr, .flags = 0, .len = 1, .buf = &reg },
        { .addr = addr, .flags = I2C_M_RD, .len = n, .buf = buf },
    };
    struct i2c_rdwr_ioctl_data xfer = { .msgs = msgs, .nmsgs = 2 };
    return ioctl(fd, I2C_RDWR, &xfer) == 2;
}

static bool find_chip(void)
{
    const uint16_t addrs[2] = { ADDR_A, ADDR_B };
    for (int i = 0; i < 2; i++) {
        uint8_t id = 0;
        addr = addrs[i];
        if (reg_read(REG_WHO_AM_I, &id, 1) && id == CHIP_ID)
            return true;
    }
    return false;
}

static bool configure(void)
{
    uint8_t c3 = CTRL3_RESET, c1 = 0, c2 = 0;

    // software reset, bit clears itself when done
    if (!reg_write(REG_CTRL3_C, CTRL3_RESET))
        return false;
    for (int i = 0; i < 50 && (c3 & CTRL3_RESET); i++) {
        sleep_ms(1);
        if (!reg_read(REG_CTRL3_C, &c3, 1))
            return false;
    }
    if (c3 & CTRL3_RESET)
        return false;

    if (!reg_write(REG_CTRL3_C, CTRL3_VAL) || !reg_write(REG_CTRL1_XL, CTRL1_VAL) ||
        !reg_write(REG_CTRL2_G, CTRL2_VAL))
        return false;

    // read back to make sure the writes went through
    if (!reg_read(REG_CTRL1_XL, &c1, 1) || !reg_read(REG_CTRL2_G, &c2, 1))
        return false;
    if (c1 != CTRL1_VAL || c2 != CTRL2_VAL)
        return false;

    // gyro needs ~70 ms to start up
    sleep_ms(80);
    return true;
}

bool imu_init(void)
{
    fd = open(IMU_BUS, O_RDWR);
    if (fd < 0) {
        perror("imu: open " IMU_BUS);
        return false;
    }
    if (!find_chip()) {
        fprintf(stderr, "imu: LSM6DSOX not found at 0x6A/0x6B\n");
        imu_close();
        return false;
    }
    if (!configure()) {
        fprintf(stderr, "imu: setup failed\n");
        imu_close();
        return false;
    }
    fprintf(stderr, "imu: LSM6DSOX at 0x%02X\n", addr);
    return true;
}

// waits for a new sample so the same one is never returned twice
bool imu_read(ImuSample *s)
{
    uint8_t status = 0, d[12];
    int16_t raw[6];

    if (fd < 0)
        return false;

    for (int i = 0;; i++) {
        if (!reg_read(REG_STATUS, &status, 1))
            return false;
        if ((status & STATUS_BOTH) == STATUS_BOTH)
            break;
        if (i >= WAIT_TRIES)
            return false;
        sleep_ms(1);
    }

    // gyro x,y,z then accel x,y,z, low byte first
    if (!reg_read(REG_OUTX_L_G, d, sizeof d))
        return false;
    for (int i = 0; i < 6; i++)
        raw[i] = (int16_t)(d[2 * i] | (d[2 * i + 1] << 8));

    s->gx = raw[0] * GYRO_DPS_PER_LSB;
    s->gy = raw[1] * GYRO_DPS_PER_LSB;
    s->gz = raw[2] * GYRO_DPS_PER_LSB;
    s->ax = raw[3] * ACC_G_PER_LSB;
    s->ay = raw[4] * ACC_G_PER_LSB;
    s->az = raw[5] * ACC_G_PER_LSB;
    s->t_ms = now_ms();
    return true;
}

void imu_close(void)
{
    if (fd >= 0)
        close(fd);
    fd = -1;
}
