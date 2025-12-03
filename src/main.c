#include "stm32l4xx_hal.h"
#include <stdlib.h>
#include <string.h>
#include "spi_bitbang_master.h"

#define LED_GPIO_PORT GPIOA
#define LED_PIN       GPIO_PIN_5
#define OUT_PORT    GPIOA
#define OUT_PIN     GPIO_PIN_8   // PA8
#define ESP_RX_BUF_LEN 256

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

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
static uint8_t esp_rx_byte;
static char esp_rx_line[ESP_RX_BUF_LEN];
static size_t esp_rx_len;

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
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void start_uart_rx(void);
static void process_line(const char *line);
static int has_type_time(const char *line);
static int parse_int_field(const char *line, const char *key, int *out);
static void echo_line_usart2(const char *line);
void Error_Handler(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    start_uart_rx();

    static int i, j;
    static int8_t posx, posy;
    static uint32_t blink_counter = 0;

    while (1)
    {
        // Extract hour and minute from ESP time
        uint8_t hour = esp_time.valid ? esp_time.hour : 0;
        uint8_t minute = esp_time.valid ? esp_time.minute : 0;

        // Calculate individual digits
        uint8_t hour_tens = (hour / 10);
        uint8_t hour_ones = (hour % 10) + 1;
        uint8_t min_tens = minute / 10;
        uint8_t min_ones = minute % 10;

        // Display HH:MM format
        // First digit (hour tens) - starting at position -80
        posx = -80;
        for (j = 0; j < 3; j++) {
            posy = 40;
            for (i = 0; i < 6; i++) {
                bitbang_character(digit_patterns[hour_tens][i][j], posy, posx);
                posy -= 10;
            }
            posx += 10;
        }

        // Second digit (hour ones) - starting at position -40
        posx = -40;
        for (j = 0; j < 3; j++) {
            posy = 40;
            for (i = 0; i < 6; i++) {
                bitbang_character(digit_patterns[hour_ones][i][j], posy, posx);
                posy -= 10;
            }
            posx += 10;
        }

        // Colon separator - blinking at position 5
        posx = 5;
        const uint8_t (*colon_pattern)[1] = (blink_counter < 50) ? colon_on : colon_off;
        posy = 40;
        for (i = 0; i < 6; i++) {
            bitbang_character(colon_pattern[i][0], posy, posx);
            posy -= 10;
        }

        // Third digit (minute tens) - starting at position 27
        posx = 27;
        for (j = 0; j < 3; j++) {
            posy = 40;
            for (i = 0; i < 6; i++) {
                bitbang_character(digit_patterns[min_tens][i][j], posy, posx);
                posy -= 10;
            }
            posx += 10;
        }

        // Fourth digit (minute ones) - starting at position 67
        posx = 67;
        for (j = 0; j < 3; j++) {
            posy = 40;
            for (i = 0; i < 6; i++) {
                bitbang_character(digit_patterns[min_ones][i][j], posy, posx);
                posy -= 10;
            }
            posx += 10;
        }

        // Subtext - two lines of characters
        bitbang_character(0x48, -30, -80);
        bitbang_character(0x48, -30, -70);
        bitbang_character(0x48, -30, -60);
        bitbang_character(0x48, -30, -50);
        bitbang_character(0x48, -30, -40);
        bitbang_character(0x48, -30, -30);
        bitbang_character(0x48, -30, -20);
        bitbang_character(0x48, -30, -10);
        bitbang_character(0x48, -30,   0);
        bitbang_character(0x48, -30,  10);
        bitbang_character(0x48, -30,  20);
        bitbang_character(0x48, -30,  30);
        bitbang_character(0x48, -30,  40);
        bitbang_character(0x48, -30,  50);
        bitbang_character(0x48, -30,  60);
        bitbang_character(0x48, -30,  70);
        bitbang_character(0x48, -30,  80);
        bitbang_character(0x48, -30,  90);

        bitbang_character(0x48, -50, -80);
        bitbang_character(0x48, -50, -70);
        bitbang_character(0x48, -50, -60);
        bitbang_character(0x48, -50, -50);
        bitbang_character(0x48, -50, -40);
        bitbang_character(0x48, -50, -30);
        bitbang_character(0x48, -50, -20);
        bitbang_character(0x48, -50, -10);
        bitbang_character(0x48, -50,   0);
        bitbang_character(0x48, -50,  10);
        bitbang_character(0x48, -50,  20);
        bitbang_character(0x48, -50,  30);
        bitbang_character(0x48, -50,  40);
        bitbang_character(0x48, -50,  50);
        bitbang_character(0x48, -50,  60);
        bitbang_character(0x48, -50,  70);
        bitbang_character(0x48, -50,  80);
        bitbang_character(0x48, -50,  90);

        // Blink counter for colon
        blink_counter++;
        if (blink_counter >= 100) {
            blink_counter = 0;
        }

        // Small delay for performance tuning
        HAL_Delay(5);
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
    HAL_UART_Init(&huart1);

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
    HAL_UART_Init(&huart2);
}

static void start_uart_rx(void)
{
    HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart1, &esp_rx_byte, 1);
    if (status != HAL_OK) {
        // Flash error - toggle PA8 rapidly if RX setup fails
        Error_Handler();
    }
}

static int parse_int_field(const char *line, const char *key, int *out)
{
    const char *p = strstr(line, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (p == end) return 0;
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
    size_t len = strlen(line);
    if (len > 0) {
        HAL_UART_Transmit(&huart2, (uint8_t *)line, len, 20);
    }
    uint8_t nl = '\n';
    HAL_UART_Transmit(&huart2, &nl, 1, 5);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) {
        return;
    }

    if (esp_rx_byte == '\n') {
        esp_rx_line[esp_rx_len] = '\0';
        if (esp_rx_len > 0) {
            process_line(esp_rx_line);
            echo_line_usart2(esp_rx_line);
        }
        esp_rx_len = 0;
    } else if (esp_rx_len < (ESP_RX_BUF_LEN - 1)) {
        esp_rx_line[esp_rx_len++] = (char)esp_rx_byte;
    } else {
        esp_rx_len = 0; // overflow, reset buffer
    }

    start_uart_rx();
}

// Add UART error callback to restart reception on error
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        start_uart_rx();
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
