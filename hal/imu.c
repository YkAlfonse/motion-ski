// LSM6DSOX accel + gyro over I2C
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include "imu.h"

#define IMU_BUS "/dev/i2c-1"
#define ADDR_A 0x6A
#define ADDR_B 0x6B

#define REG_FIFO_CTRL3 0x09
#define REG_FIFO_CTRL4 0x0A
#define REG_WHO_AM_I 0x0F
#define REG_CTRL1_XL 0x10
#define REG_CTRL2_G 0x11
#define REG_CTRL3_C 0x12
#define REG_STATUS 0x1E
#define REG_OUTX_L_G 0x22
#define REG_FIFO_STATUS1 0x3A
#define REG_FIFO_DATA 0x78

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

// max wait for new data, 1 ms per try
#define WAIT_TRIES 20

// batch gyro and accel at 104 Hz, continuous mode (oldest dropped if full)
#define FIFO_CTRL3_VAL 0x44
#define FIFO_CTRL4_VAL 0x06
#define FIFO_BYPASS 0x00
// FIFO_STATUS2 bits
#define FIFO_DIFF_HI 0x03
#define FIFO_OVR 0x48
// word tags
#define TAG_GYRO 0x01
#define TAG_ACC 0x02
// most words read per batch, 2 words per sample
#define BATCH_WORDS 32
#define QUEUE_LEN (BATCH_WORDS / 2)
#define PERIOD_MS (1000.0 / 104.0)

static int fd = -1;
static uint16_t addr;
static bool use_fifo;

// samples read from the fifo but not handed out yet
static ImuSample queue[QUEUE_LEN];
static int q_head, q_len;
// half built sample, waiting for its other word
static ImuSample part;
static bool have_g, have_a;
static int part_cnt, last_cnt;
// time of the last sample, kept on the chip's 104 Hz steps
static double t_est;

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

static bool setup_fifo(void)
{
    uint8_t c3 = 0, c4 = 0;

    // bypass first so the start up samples are thrown out
    if (!reg_write(REG_FIFO_CTRL4, FIFO_BYPASS) ||
        !reg_write(REG_FIFO_CTRL3, FIFO_CTRL3_VAL) ||
        !reg_write(REG_FIFO_CTRL4, FIFO_CTRL4_VAL))
        return false;
    if (!reg_read(REG_FIFO_CTRL3, &c3, 1) || !reg_read(REG_FIFO_CTRL4, &c4, 1))
        return false;

    q_head = q_len = 0;
    have_g = have_a = false;
    part_cnt = last_cnt = -1;
    t_est = 0;
    return c3 == FIFO_CTRL3_VAL && c4 == FIFO_CTRL4_VAL;
}

// gives a sample its time. steps 1/104 s from the last one and slowly
// follows the board clock so it doesn't drift from now_ms()
static void stamp(ImuSample *s, double target, int skipped)
{
    double step = PERIOD_MS * (1 + skipped);

    if (t_est == 0 || target - (t_est + step) > 100 ||
        target - (t_est + step) < -100)
        t_est = target;
    else
        t_est += step + (target - (t_est + step)) * 0.02;
    s->t_ms = (uint64_t)(t_est + 0.5);
}

static void push(ImuSample *s)
{
    if (q_len == QUEUE_LEN)
        return;
    queue[(q_head + q_len) % QUEUE_LEN] = *s;
    q_len++;
}

// reads what is waiting in the fifo into the queue
static bool fill_queue(void)
{
    uint8_t st[2], w[7];

    if (!reg_read(REG_FIFO_STATUS1, st, 2))
        return false;

    uint64_t now = now_ms();
    int words = st[0] | ((st[1] & FIFO_DIFF_HI) << 8);
    bool overrun = st[1] & FIFO_OVR;
    int n = words < BATCH_WORDS ? words : BATCH_WORDS;

    // samples were lost, start the timing over
    if (overrun)
        t_est = 0;

    for (int i = 0; i < n; i++) {
        if (!reg_read(REG_FIFO_DATA, w, sizeof w))
            return false;

        // tag byte: sensor in bits 7-3, slot counter in bits 2-1
        int tag = w[0] >> 3, cnt = (w[0] >> 1) & 0x03;
        float x = (int16_t)(w[1] | (w[2] << 8));
        float y = (int16_t)(w[3] | (w[4] << 8));
        float z = (int16_t)(w[5] | (w[6] << 8));

        // new time slot, drop a half built sample from the old one
        if (cnt != part_cnt) {
            have_g = have_a = false;
            part_cnt = cnt;
        }

        if (tag == TAG_GYRO) {
            part.gx = x * GYRO_DPS_PER_LSB;
            part.gy = y * GYRO_DPS_PER_LSB;
            part.gz = z * GYRO_DPS_PER_LSB;
            have_g = true;
        } else if (tag == TAG_ACC) {
            part.ax = x * ACC_G_PER_LSB;
            part.ay = y * ACC_G_PER_LSB;
            part.az = z * ACC_G_PER_LSB;
            have_a = true;
        }

        if (have_g && have_a) {
            // counter jumps more than 1 if a slot went missing
            int skipped = last_cnt < 0 ? 0 : (cnt - last_cnt - 1) & 0x03;
            // newest sample in the fifo is about now
            double target = now - ((words - 1 - i) / 2) * PERIOD_MS;

            stamp(&part, target, skipped);
            push(&part);
            have_g = have_a = false;
            last_cnt = cnt;
        }
    }
    return true;
}

static bool read_fifo(ImuSample *s)
{
    for (int i = 0;; i++) {
        if (q_len > 0) {
            *s = queue[q_head];
            q_head = (q_head + 1) % QUEUE_LEN;
            q_len--;
            return true;
        }
        if (!fill_queue())
            return false;
        if (q_len > 0)
            continue;
        if (i >= WAIT_TRIES / 2)
            return false;
        sleep_ms(2);
    }
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

    // IMU_NO_FIFO=1 uses the old one at a time reads, to compare
    const char *no_fifo = getenv("IMU_NO_FIFO");
    use_fifo = !(no_fifo && no_fifo[0] == '1');
    if (use_fifo && !setup_fifo()) {
        fprintf(stderr, "imu: fifo setup failed, reading one at a time\n");
        reg_write(REG_FIFO_CTRL4, FIFO_BYPASS);
        use_fifo = false;
    }

    fprintf(stderr, "imu: LSM6DSOX at 0x%02X, %s\n", addr,
            use_fifo ? "fifo" : "polling");
    return true;
}

// waits for a new sample so the same one is never returned twice
static bool read_polled(ImuSample *s)
{
    uint8_t status = 0, d[12];
    int16_t raw[6];

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

bool imu_read(ImuSample *s)
{
    if (fd < 0 || !s)
        return false;
    return use_fifo ? read_fifo(s) : read_polled(s);
}

void imu_close(void)
{
    if (fd >= 0)
        close(fd);
    fd = -1;
}
