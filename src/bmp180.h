/*
 * bmp180.h
 *
 * Bosch BMP180 temperature / pressure sensor over I2C (HW-706 / GY-68 style breakout).
 * Non-blocking: bmp180_poll() runs a small state machine that waits out the conversion
 * times with HAL_GetTick() instead of HAL_Delay(), so the display loop never stalls.
 */

#ifndef BMP180_H_
#define BMP180_H_

#include <stdint.h>
#include "stm32l4xx_hal.h"

#define BMP180_I2C_ADDR      (0x77 << 1)
#define BMP180_CHIP_ID       0x55
#define BMP180_SEA_LEVEL_PA  101325

typedef struct {
    int16_t  temperature_dc;  // 0.1 degC
    int32_t  pressure_pa;     // Pa
    float    temperature_c;
    float    pressure_hpa;
    float    altitude_m;      // relative to BMP180_SEA_LEVEL_PA
    uint32_t tick;            // HAL tick of the measurement
    uint8_t  valid;
} bmp180_reading_t;

// Latest measurement, also handy as a debugger live-watch variable
extern volatile bmp180_reading_t bmp180_latest;

/**
 * @brief Probe the sensor and read its calibration EEPROM.
 * @param hi2c        initialised I2C handle the sensor is on
 * @param oss         oversampling setting 0..3 (higher = slower, less noise)
 * @param interval_ms time between measurements
 * @return 0 on success, -1 if the sensor did not answer correctly
 */
int bmp180_init(I2C_HandleTypeDef *hi2c, uint8_t oss, uint32_t interval_ms);

/**
 * @brief Advance the measurement state machine; call often from the main loop.
 *        Each call does at most one short I2C transaction. Re-probes the sensor
 *        every interval if it is missing or a transfer failed.
 */
void bmp180_poll(void);

/**
 * @brief Copy the latest measurement.
 * @return 0 if a valid reading is available, -1 otherwise
 */
int bmp180_get(bmp180_reading_t *out);

#endif /* BMP180_H_ */
