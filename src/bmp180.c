/*
 * bmp180.c
 *
 * Integer compensation follows the BMP180 datasheet (BST-BMP180-DS000), section 3.5.
 */

#include "bmp180.h"
#include <math.h>
#include <string.h>

#define REG_CALIB     0xAA
#define REG_CHIP_ID   0xD0
#define REG_CTRL_MEAS 0xF4
#define REG_OUT_MSB   0xF6

#define CMD_TEMP      0x2E
#define CMD_PRESS     0x34

#define I2C_TIMEOUT_MS 10

typedef enum {
    ST_ABSENT,      // not probed / lost, re-probe every interval
    ST_IDLE,        // waiting for next interval
    ST_TEMP_WAIT,   // temperature conversion running
    ST_PRESS_WAIT,  // pressure conversion running
} bmp180_state_t;

static struct {
    int16_t  ac1, ac2, ac3;
    uint16_t ac4, ac5, ac6;
    int16_t  b1, b2, mb, mc, md;
} cal;

static I2C_HandleTypeDef *bus;
static uint8_t  oss_setting;
static uint32_t interval;
static bmp180_state_t state = ST_ABSENT;
static uint32_t state_tick;
static uint32_t last_start_tick;
static int32_t  raw_ut;

volatile bmp180_reading_t bmp180_latest;

// Max conversion times in ms per datasheet table 8, indexed by oss
static const uint8_t press_wait_ms[4] = {5, 8, 14, 26};
static const uint8_t temp_wait_ms = 5;

static int write_reg(uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(bus, BMP180_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                             &value, 1, I2C_TIMEOUT_MS) == HAL_OK ? 0 : -1;
}

static int read_regs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(bus, BMP180_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                            buf, len, I2C_TIMEOUT_MS) == HAL_OK ? 0 : -1;
}

static int probe(void)
{
    uint8_t id = 0;
    if (read_regs(REG_CHIP_ID, &id, 1) != 0 || id != BMP180_CHIP_ID) {
        return -1;
    }

    uint8_t c[22];
    if (read_regs(REG_CALIB, c, sizeof(c)) != 0) {
        return -1;
    }

    // Every word must be neither 0x0000 nor 0xFFFF (datasheet communication check)
    for (int i = 0; i < 22; i += 2) {
        uint16_t w = (uint16_t)((c[i] << 8) | c[i + 1]);
        if (w == 0x0000 || w == 0xFFFF) {
            return -1;
        }
    }

    cal.ac1 = (int16_t)((c[0] << 8) | c[1]);
    cal.ac2 = (int16_t)((c[2] << 8) | c[3]);
    cal.ac3 = (int16_t)((c[4] << 8) | c[5]);
    cal.ac4 = (uint16_t)((c[6] << 8) | c[7]);
    cal.ac5 = (uint16_t)((c[8] << 8) | c[9]);
    cal.ac6 = (uint16_t)((c[10] << 8) | c[11]);
    cal.b1  = (int16_t)((c[12] << 8) | c[13]);
    cal.b2  = (int16_t)((c[14] << 8) | c[15]);
    cal.mb  = (int16_t)((c[16] << 8) | c[17]);
    cal.mc  = (int16_t)((c[18] << 8) | c[19]);
    cal.md  = (int16_t)((c[20] << 8) | c[21]);
    return 0;
}

static void compensate(int32_t ut, int32_t up)
{
    int32_t x1 = ((ut - (int32_t)cal.ac6) * (int32_t)cal.ac5) >> 15;
    int32_t x2 = ((int32_t)cal.mc * 2048) / (x1 + cal.md);
    int32_t b5 = x1 + x2;
    int32_t t  = (b5 + 8) >> 4;

    int32_t b6 = b5 - 4000;
    x1 = (cal.b2 * ((b6 * b6) >> 12)) >> 11;
    x2 = (cal.ac2 * b6) >> 11;
    int32_t x3 = x1 + x2;
    int32_t b3 = ((((int32_t)cal.ac1 * 4 + x3) << oss_setting) + 2) / 4;
    x1 = (cal.ac3 * b6) >> 13;
    x2 = (cal.b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    uint32_t b4 = ((uint32_t)cal.ac4 * (uint32_t)(x3 + 32768)) >> 15;
    uint32_t b7 = ((uint32_t)up - (uint32_t)b3) * (50000UL >> oss_setting);
    int32_t p = (b7 < 0x80000000UL) ? (int32_t)((b7 * 2) / b4) : (int32_t)((b7 / b4) * 2);
    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p += (x1 + x2 + 3791) >> 4;

    bmp180_latest.temperature_dc = (int16_t)t;
    bmp180_latest.pressure_pa = p;
    bmp180_latest.temperature_c = t / 10.0f;
    bmp180_latest.pressure_hpa = p / 100.0f;
    bmp180_latest.altitude_m = 44330.0f * (1.0f - powf((float)p / BMP180_SEA_LEVEL_PA, 1.0f / 5.255f));
    bmp180_latest.tick = HAL_GetTick();
    bmp180_latest.valid = 1;
}

static void fail(void)
{
    bmp180_latest.valid = 0;
    state = ST_ABSENT;
}

int bmp180_init(I2C_HandleTypeDef *hi2c, uint8_t oss, uint32_t interval_ms)
{
    bus = hi2c;
    oss_setting = oss > 3 ? 3 : oss;
    interval = interval_ms;
    last_start_tick = HAL_GetTick();

    if (probe() != 0) {
        fail();
        return -1;
    }
    // Start the first measurement right away
    last_start_tick -= interval;
    state = ST_IDLE;
    return 0;
}

void bmp180_poll(void)
{
    if (bus == NULL) {
        return;
    }

    uint32_t now = HAL_GetTick();

    switch (state) {
    case ST_ABSENT:
        if (now - last_start_tick >= interval) {
            last_start_tick = now;
            if (probe() == 0) {
                state = ST_IDLE;
            }
        }
        break;

    case ST_IDLE:
        if (now - last_start_tick >= interval) {
            last_start_tick = now;
            if (write_reg(REG_CTRL_MEAS, CMD_TEMP) != 0) {
                fail();
                break;
            }
            state_tick = now;
            state = ST_TEMP_WAIT;
        }
        break;

    case ST_TEMP_WAIT:
        // Strictly greater: a 1 ms tick difference can be as little as just over 0 ms
        if (now - state_tick > temp_wait_ms) {
            uint8_t b[2];
            if (read_regs(REG_OUT_MSB, b, 2) != 0 ||
                write_reg(REG_CTRL_MEAS, (uint8_t)(CMD_PRESS | (oss_setting << 6))) != 0) {
                fail();
                break;
            }
            raw_ut = (b[0] << 8) | b[1];
            state_tick = now;
            state = ST_PRESS_WAIT;
        }
        break;

    case ST_PRESS_WAIT:
        if (now - state_tick > press_wait_ms[oss_setting]) {
            uint8_t b[3];
            if (read_regs(REG_OUT_MSB, b, 3) != 0) {
                fail();
                break;
            }
            int32_t up = (int32_t)(((uint32_t)b[0] << 16 | (uint32_t)b[1] << 8 | b[2]) >> (8 - oss_setting));
            compensate(raw_ut, up);
            state = ST_IDLE;
        }
        break;
    }
}

int bmp180_get(bmp180_reading_t *out)
{
    // Only written from bmp180_poll() in thread context, so a plain copy is consistent
    memcpy(out, (const void *)&bmp180_latest, sizeof(*out));
    return out->valid ? 0 : -1;
}
