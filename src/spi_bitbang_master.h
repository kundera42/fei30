/*
 * spi_bitbang_master.h
 *
 * Aircraft display bit-bang protocol driver
 * Drives DATA (PA10) and CLOCK (PA11) pins to communicate with line driver
 */

#ifndef SPI_BITBANG_MASTER_H_
#define SPI_BITBANG_MASTER_H_

#include <stdint.h>
#include "stm32l4xx_hal.h"

/*
 * Configuration
 */
#define LSB_FIRST 0
#define DELAY_CYCLES 4

/*
 * Protocol constants
 */
static const uint8_t zerobyte = 0x00;
static const uint8_t syncmrkr = 0x02;

/*
 * Hardware Definitions for Pin Setting and Resetting
 * PA10 = DATA, PA11 = CLOCK
 */
#define MOSI_LOW  GPIOA->BSRR = GPIO_BSRR_BS_10    // Set PA10 (DATA) low
#define MOSI_HIGH GPIOA->BSRR = GPIO_BSRR_BR_10    // Set PA10 (DATA) high

#define SCK_LOW  GPIOA->BSRR = GPIO_BSRR_BR_11     // Set PA11 (CLOCK) low
#define SCK_HIGH GPIOA->BSRR = GPIO_BSRR_BS_11     // Set PA11 (CLOCK) high

#define DELAY for (uint32_t i = 0; i < DELAY_CYCLES; i++) __NOP()

/**
 * @brief Sends a character to the aircraft display at specified position
 * @param data Character/symbol to display
 * @param posy Y position on display
 * @param posx X position on display
 */
__attribute__((optimize("O0")))
static inline void bitbang_character(uint8_t data, int8_t posy, int8_t posx)
{
    // 8 clocks, no data (preamble)
    for (uint8_t i = 0; i < 8; i++)
    {
        if ((zerobyte << i) & 0x80)
        {
            MOSI_HIGH;
        }
        else
        {
            MOSI_LOW;
        }

        SCK_HIGH;
        DELAY;
        SCK_LOW;
        DELAY;
    }

    // Send 0x02 (sync marker)
    for (uint8_t i = 0; i < 8; i++)
    {
        if ((syncmrkr << i) & 0x80)
        {
            MOSI_HIGH;
        }
        else
        {
            MOSI_LOW;
        }

        SCK_HIGH;
        DELAY;
        SCK_LOW;
        DELAY;
    }

    // Send character data
    for (uint8_t i = 0; i < 8; i++)
    {
        if ((data << i) & 0x80)
        {
            MOSI_HIGH;
        }
        else
        {
            MOSI_LOW;
        }

        SCK_HIGH;
        DELAY;
        SCK_LOW;
        DELAY;
    }

    MOSI_LOW;

    // Send Y position
    for (uint8_t i = 0; i < 8; i++)
    {
        if ((posy << i) & 0x80)
        {
            MOSI_HIGH;
        }
        else
        {
            MOSI_LOW;
        }

        SCK_HIGH;
        DELAY;
        SCK_LOW;
        DELAY;
    }

    MOSI_LOW;

    // Send X position
    for (uint8_t i = 0; i < 8; i++)
    {
        if ((posx << i) & 0x80)
        {
            MOSI_HIGH;
        }
        else
        {
            MOSI_LOW;
        }

        SCK_HIGH;
        DELAY;
        SCK_LOW;
        DELAY;
    }

    MOSI_LOW;

    // Artificial delay of 1 byte
    for (uint8_t i = 0; i < 16; i++)
    {
        DELAY;
    }

    // 8 clocks, no data (postamble)
    for (uint8_t i = 0; i < 8; i++)
    {
        if ((zerobyte << i) & 0x80)
        {
            MOSI_HIGH;
        }
        else
        {
            MOSI_LOW;
        }

        SCK_HIGH;
        DELAY;
        SCK_LOW;
        DELAY;
    }

    // Artificial delay of ~70us
    for (uint8_t i = 0; i < 140; i++)
    {
        // TODO: check this delay, can it be optimized? If the requirement is 70ns then we should measure on the scope it indeed is. 
        DELAY;
    }
}

#endif /* SPI_BITBANG_MASTER_H_ */
