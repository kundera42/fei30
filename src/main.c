#include "stm32l4xx_hal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "spi_bitbang_master.h"

#define M_PI 3.14159265358979323846

#define LED_GPIO_PORT GPIOA
#define LED_PIN       GPIO_PIN_5
#define OUT_PORT    GPIOA
#define OUT_PIN     GPIO_PIN_8   // PA8
#define ESP_RX_BUF_LEN 256

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart2_tx;

typedef struct {
    uint32_t epoch;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t valid;
} EspTime;

static volatile EspTime esp_time;
static char esp_rx_line[ESP_RX_BUF_LEN];
static volatile uint8_t uart_rx_busy = 0;
static char echo_buffer[ESP_RX_BUF_LEN + 1];  // +1 for newline
static volatile uint8_t echo_busy = 0;

int testcounter = 0;

// Character matrices for aircraft display digits 0-9
static const uint8_t digit_patterns[10][6][3] = {
    // Digit 0
    {
        {0xaa, 0x9b, 0xa9},
        {0x9c, 0x0a, 0x9d},
        {0x9c, 0x0a, 0x9d},
        {0x9c, 0x0a, 0x9d},
        {0x9c, 0x0a, 0x9d},
        {0xa9, 0x9a, 0xaa}
    },
    // Digit 1
    {
        {0x20, 0xd9, 0x20},
        {0x9a, 0x95, 0x20},
        {0x20, 0x95, 0x20},
        {0x20, 0x95, 0x20},
        {0x20, 0x95, 0x20},
        {0x20, 0x9a, 0x20}
    },
    // Digit 2
    {
        {0xaa, 0x9b, 0xa9},
        {0x20, 0x0a, 0x9d},
        {0xaa, 0x9b, 0xa9},
        {0x9c, 0x0a, 0x20},
        {0x9c, 0x0a, 0x20},
        {0xa0, 0xa1, 0xa2}
    },
    // Digit 3
    {
        {0xaa, 0x9b, 0xa9},
        {0x20, 0x0a, 0x9d},
        {0xaa, 0x9b, 0xa9},
        {0x20, 0x0a, 0x9d},
        {0x20, 0x0a, 0x9d},
        {0xa0, 0xa1, 0xa2}
    },
    // Digit 4
    {
        {0x20, 0x20, 0x20},
        {0x9c, 0x0a, 0x9d},
        {0xaa, 0x9b, 0xa9},
        {0x20, 0x0a, 0x9d},
        {0x20, 0x0a, 0x9d},
        {0x20, 0x20, 0x20}
    },
    // Digit 5
    {
        {0xaa, 0x9b, 0xa9},
        {0x9c, 0x0a, 0x0a},
        {0x9c, 0x96, 0x0a},
        {0x0a, 0x0a, 0x9d},
        {0x0a, 0x0a, 0x9d},
        {0xa9, 0x9a, 0xaa}
    },
    // Digit 6
    {
        {0xaa, 0x9b, 0xa9},
        {0x9c, 0x0a, 0x20},
        {0xaa, 0x9b, 0xa9},
        {0x9c, 0x0a, 0x9d},
        {0x9c, 0x0a, 0x9d},
        {0xa0, 0xa1, 0xa2}
    },
    // Digit 7
    {
        {0xaa, 0x9b, 0xa9},
        {0x20, 0x0a, 0x9d},
        {0x20, 0x0a, 0x9d},
        {0x20, 0x0a, 0x9d},
        {0x20, 0x0a, 0x9d},
        {0x20, 0x20, 0x20}
    },
    // Digit 8
    {
        {0xaa, 0x9b, 0xa9},
        {0x9c, 0x0a, 0x9d},
        {0xaa, 0x9b, 0xa9},
        {0x9c, 0x0a, 0x9d},
        {0x9c, 0x0a, 0x9d},
        {0xa0, 0xa1, 0xa2}
    },
    // Digit 9
    {
        {0xaa, 0x9b, 0xa9},
        {0x9c, 0x0a, 0x9d},
        {0x9c, 0x96, 0x9d},
        {0x0a, 0x0a, 0x9d},
        {0x0a, 0x0a, 0x9d},
        {0xa9, 0x9a, 0xaa}
    }
};

// Colon separator
static const uint8_t colon_on[6][1] = {
    {0x20},
    {0xec},
    {0x20},
    {0x20},
    {0xee},
    {0x20}
};

static const uint8_t colon_off[6][1] = {
    {0x20},
    {0x20},
    {0x20},
    {0x20},
    {0x20},
    {0x20}
};

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void start_uart_rx(void);
static void process_line(const char *line);
static int has_type_time(const char *line);
static int parse_int_field(const char *line, const char *key, int *out);
static void echo_line_usart2(const char *line);
static void draw_line(int8_t x0, int8_t y0, int8_t x1, int8_t y1, uint8_t character);
static void draw_clock_hands(uint8_t hour, uint8_t minute);
void Error_Handler(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    start_uart_rx();

    static int i, j;
    static int8_t posx, posy;
    static uint32_t blink_counter = 0;
    static float circle_angle = 0.0f;
    static uint32_t anim_counter = 0;
    static uint8_t sim_hour = 2;
    static uint8_t sim_minute = 10;

    while (1)
    {
        // Extract hour and minute from ESP time
        uint8_t hour = esp_time.valid ? esp_time.hour : 0;
        uint8_t minute = esp_time.valid ? esp_time.minute : 0;

        // Animate clock hands for testing
        anim_counter++;
        if (anim_counter >= 20) {  // Update every ~50 frames
            anim_counter = 0;
            sim_minute++;
            if (sim_minute >= 60) {
                sim_minute = 0;
                sim_hour++;
                if (sim_hour >= 12) {
                    sim_hour = 0;
                }
            }
        }

        // Calculate individual digits
        uint8_t hour_tens = (hour / 10);
        uint8_t hour_ones = (hour % 10) + 1;
        uint8_t min_tens = minute / 10;
        uint8_t min_ones = minute % 10;

        // // Display HH:MM format
        // // First digit (hour tens) - starting at position -80
        // posx = -80;
        // for (j = 0; j < 3; j++) {
        //     posy = 40;
        //     for (i = 0; i < 6; i++) {
        //         bitbang_character(digit_patterns[hour_tens][i][j], posy, posx);
        //         posy -= 10;
        //     }
        //     posx += 10;
        // }

        // // Second digit (hour ones) - starting at position -40
        // posx = -40;
        // for (j = 0; j < 3; j++) {
        //     posy = 40;
        //     for (i = 0; i < 6; i++) {
        //         bitbang_character(digit_patterns[hour_ones][i][j], posy, posx);
        //         posy -= 10;
        //     }
        //     posx += 10;
        // }

        // // Colon separator - blinking at position 5
        // posx = 5;
        // const uint8_t (*colon_pattern)[1] = (blink_counter < 50) ? colon_on : colon_off;
        // posy = 40;
        // for (i = 0; i < 6; i++) {
        //     bitbang_character(colon_pattern[i][0], posy, posx);
        //     posy -= 10;
        // }

        // // Third digit (minute tens) - starting at position 27
        // posx = 27;
        // for (j = 0; j < 3; j++) {
        //     posy = 40;
        //     for (i = 0; i < 6; i++) {
        //         bitbang_character(digit_patterns[min_tens][i][j], posy, posx);
        //         posy -= 10;
        //     }
        //     posx += 10;
        // }

        // // Fourth digit (minute ones) - starting at position 67
        // posx = 67;
        // for (j = 0; j < 3; j++) {
        //     posy = 40;
        //     for (i = 0; i < 6; i++) {
        //         bitbang_character(digit_patterns[min_ones][i][j], posy, posx);
        //         posy -= 10;
        //     }
        //     posx += 10;
        // }

        // Draw hour markers (1-12) at clock positions
        const float hour_radius = 100.0f;
        for (int hour = 1; hour <= 12; hour++) {
            // Calculate angle for this hour (12 o'clock = top = 90°, clockwise)
            // angle = 90° - (hour * 30°) for standard clock positions
            float hour_angle = (M_PI / 2.0f) - ((hour % 12) * M_PI / 6.0f);

            int8_t hour_x = (int8_t)(hour_radius * cosf(hour_angle));
            int8_t hour_y = (int8_t)(hour_radius * sinf(hour_angle));

            // Display digit: 0x31='1', 0x32='2', etc. For 10-12, just use first digit
            uint8_t digit_char;
            if (hour < 10) {
                digit_char = 0x30 + hour;  // '1' through '9'
            } else if (hour == 10) {
                digit_char = 0x31;  // '1' for position 10
            } else if (hour == 11) {
                digit_char = 0x31;  // '1' for position 11
            } else {
                digit_char = 0x31;  // '1' for position 12
            }

            bitbang_character(digit_char, hour_y, hour_x);
        }

        // Draw clock hands using pixel character 0xD5 (animated for testing)
        draw_clock_hands(sim_hour, sim_minute);

        bitbang_character(0xE2, 0,0);
        // Circular motion - character moves in a circle around the display
        // const float orbit_radius = 60.0f;
        // int8_t circle_x = (int8_t)(orbit_radius * cosf(circle_angle));
        // int8_t circle_y = (int8_t)(orbit_radius * sinf(circle_angle));

        // bitbang_character(0xE2, circle_y, circle_x);

        // Update angle - increment by PI/1000 per frame for ~2 second rotation
        // circle_angle -= (M_PI / 10.0f);
        // if (circle_angle <= (2.0f * M_PI)) {
        //     circle_angle += (2.0f * M_PI);
        // }

        // Blink counter for colon
        blink_counter++;
        if (blink_counter >= 100) {
            blink_counter = 0;
        }

        // Small delay for performance tuning
        // HAL_Delay(1);
    }
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = OUT_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH; // nice clean edges
    HAL_GPIO_Init(OUT_PORT, &gpio);

    // PA10 (DATA) and PA11 (CLOCK) for bit-bang aircraft display protocol
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void MX_DMA_Init(void)
{
    // Enable DMA1 clock
    __HAL_RCC_DMA1_CLK_ENABLE();

    // Configure DMA for USART1 RX (DMA1 Channel 5)
    hdma_usart1_rx.Instance = DMA1_Channel5;
    hdma_usart1_rx.Init.Request = DMA_REQUEST_2;  // USART1_RX
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_NORMAL;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_LOW;

    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK) {
        Error_Handler();
    }

    // Disable half-transfer interrupt (only want complete transfer + idle)
    __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);

    // DMA interrupt priority
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

    // Configure DMA for USART2 TX (DMA1 Channel 7)
    hdma_usart2_tx.Instance = DMA1_Channel7;
    hdma_usart2_tx.Init.Request = DMA_REQUEST_2;  // USART2_TX
    hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_tx.Init.Mode = DMA_NORMAL;
    hdma_usart2_tx.Init.Priority = DMA_PRIORITY_LOW;

    if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK) {
        Error_Handler();
    }

    // DMA interrupt priority
    HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);
}

static void MX_USART1_UART_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOB, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    // Link DMA handle to UART
    __HAL_LINKDMA(&huart1, hdmarx, hdma_usart1_rx);

    HAL_UART_Init(&huart1);

    // Enable USART1 interrupt for DMA callbacks
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static void MX_USART2_UART_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_2; // PA2 -> VCP TX
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    // Link DMA handle to UART2 TX
    __HAL_LINKDMA(&huart2, hdmatx, hdma_usart2_tx);

    HAL_UART_Init(&huart2);

    // Enable USART2 interrupt for DMA callbacks
    HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

static void start_uart_rx(void)
{
    // Use DMA with idle line detection - receives until line goes idle (message complete)
    HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, (uint8_t *)esp_rx_line, ESP_RX_BUF_LEN);
    if (status != HAL_OK) {
        // Flash error 
        Error_Handler();
    }
}

static int parse_int_field(const char *line, const char *key, int *out)
{
    const char *p = strstr(line, key);
    
    if (!p) 
        return 0;
    
    p = strchr(p, ':');
    
    if (!p) 
        return 0;
    
    p++;
    
    while (*p == ' ' || *p == '\t') 
        p++;
    
    char *end = NULL;
    long v = strtol(p, &end, 10);
    
    if (p == end) 
        return 0;
    
    *out = (int)v;
    
    return 1;
}

static int has_type_time(const char *line)
{
    return strstr(line, "\"type\":\"time\"") != NULL;
}

static void process_line(const char *line)
{
    if (!has_type_time(line)) {
        return;
    }

    int epoch, year, month, day, hour, minute, second;
    if (parse_int_field(line, "\"epoch\"", &epoch) &&
        parse_int_field(line, "\"year\"", &year) &&
        parse_int_field(line, "\"month\"", &month) &&
        parse_int_field(line, "\"day\"", &day) &&
        parse_int_field(line, "\"hour\"", &hour) &&
        parse_int_field(line, "\"minute\"", &minute) &&
        parse_int_field(line, "\"second\"", &second)) {

        EspTime t = {
            .epoch = (uint32_t)epoch,
            .year = (uint16_t)year,
            .month = (uint8_t)month,
            .day = (uint8_t)day,
            .hour = (uint8_t)hour,
            .minute = (uint8_t)minute,
            .second = (uint8_t)second,
            .valid = 1
        };
        esp_time = t;
    }
}

static void echo_line_usart2(const char *line)
{
    // Skip if previous echo still in progress
    if (echo_busy) {
        return;
    }

    size_t len = strlen(line);
    if (len > 0 && len < ESP_RX_BUF_LEN) {
        echo_busy = 1;

        // Copy to echo buffer and add newline
        memcpy(echo_buffer, line, len);
        echo_buffer[len] = '\n';

        // Non-blocking DMA transmit
        HAL_UART_Transmit_DMA(&huart2, (uint8_t *)echo_buffer, len + 1);
    }
}

// DMA RX Event callback - called when idle line detected or buffer full
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance != USART1) {
        return;
    }

    // Prevent duplicate processing if callback fires multiple times
    if (uart_rx_busy) {
        return;
    }
    uart_rx_busy = 1;

    // Toggle PA8 to show callback is being called
    HAL_GPIO_TogglePin(OUT_PORT, OUT_PIN);

    // Null-terminate the received data
    if (Size > 0 && Size < ESP_RX_BUF_LEN) {
        esp_rx_line[Size] = '\0';

        // Process and echo the line
        process_line(esp_rx_line);
        echo_line_usart2(esp_rx_line);  // Non-blocking DMA echo
    }

    // Restart DMA reception for next message
    HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, (uint8_t *)esp_rx_line, ESP_RX_BUF_LEN);

    uart_rx_busy = 0;
}

// UART TX complete callback - called when DMA finishes transmitting
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        echo_busy = 0;  // Ready for next echo
    }
}

// UART error callback to restart reception on error
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        start_uart_rx();
    } else if (huart->Instance == USART2) {
        echo_busy = 0;  // Clear busy flag on error
    }
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
#ifdef PWR_REGULATOR_VOLTAGE_SCALE1_BOOST
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);
#else
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);
#endif

    osc.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    osc.MSIState = RCC_MSI_ON;
    osc.MSIClockRange = RCC_MSIRANGE_6;
    osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    osc.PLL.PLLM = 1;
    osc.PLL.PLLN = 40;
    osc.PLL.PLLR = RCC_PLLR_DIV2;
    osc.PLL.PLLP = RCC_PLLP_DIV7;
    osc.PLL.PLLQ = RCC_PLLQ_DIV4;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief Draws a line using Bresenham's algorithm with pixel character 0xD5
 * @param x0 Starting X coordinate
 * @param y0 Starting Y coordinate
 * @param x1 Ending X coordinate
 * @param y1 Ending Y coordinate
 * @param character Character to use for drawing (typically 0xD5 for pixels)
 */
static void draw_line(int8_t x0, int8_t y0, int8_t x1, int8_t y1, uint8_t character)
{
    int16_t dx = abs(x1 - x0);  // Use int16_t to prevent overflow
    int16_t dy = abs(y1 - y0);
    int8_t sx = (x0 < x1) ? 1 : -1;
    int8_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;       // Error term needs int16_t
    int8_t x = x0;
    int8_t y = y0;
    uint8_t step_counter = 0;
    const uint8_t step_size = 5;  // Draw every 3rd pixel

    while (1) {
        // Draw pixel at current position (only every Nth step)
        if (step_counter % step_size == 0) {
            bitbang_character(character, y, x);
        }
        step_counter++;

        // Check if we've reached the end point
        if (x == x1 && y == y1) {
            break;
        }

        int16_t e2 = 2 * err;  // This can overflow int8_t, needs int16_t

        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }

        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

/**
 * @brief Draws hour and minute hands on the clock display
 * @param hour Current hour (0-23)
 * @param minute Current minute (0-59)
 */
static void draw_clock_hands(uint8_t hour, uint8_t minute)
{
    // Convert 24-hour to 12-hour format for clock hand positioning
    uint8_t hour_12 = hour % 12;

    // Calculate minute hand angle (12 o'clock = top = 90°, clockwise)
    // Each minute = 6 degrees, minute hand length = 80 pixels
    float minute_angle = (M_PI / 2.0f) - (minute * M_PI / 30.0f);
    int8_t minute_x = (int8_t)(80.0f * cosf(minute_angle));
    int8_t minute_y = (int8_t)(80.0f * sinf(minute_angle));

    // Calculate hour hand angle
    // Each hour = 30 degrees, plus offset for minutes (0.5 degrees per minute)
    // Hour hand length = 50 pixels
    float hour_angle = (M_PI / 2.0f) - ((hour_12 * M_PI / 6.0f) + (minute * M_PI / 360.0f));
    int8_t hour_x = (int8_t)(50.0f * cosf(hour_angle));
    int8_t hour_y = (int8_t)(50.0f * sinf(hour_angle));

    // Draw minute hand (from center to minute position) with pixel character 0xD5
    draw_line(0, 0, minute_x, minute_y, 0xD5);

    // Draw hour hand (from center to hour position) with pixel character 0xD5
    draw_line(0, 0, hour_x, hour_y, 0xD5);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    Error_Handler();
}
#endif
