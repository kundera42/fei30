// Transparent UART bridge: ST-LINK VCP (USART2: PA2/PA15) <-> ESP-01 (USART1: PB6/PB7)
// at 115200 8N1. Used to talk to / flash the soldered ESP-01 through COM port of the ST-LINK.
// Bare register code, no HAL, so the bridge stays tiny and has no interrupt latency surprises.

#include "stm32l4xx.h"

#define BAUD     115200U
#define SYSCLK   48000000U
#define RING_LEN 512U

typedef struct {
    uint8_t buf[RING_LEN];
    uint16_t head;
    uint16_t tail;
} ring_t;

static ring_t to_esp;
static ring_t to_pc;

// Error counters, inspect over SWD: [0]=framing [1]=noise [2]=overrun [3]=ring full
volatile uint32_t err_usart1[4];
volatile uint32_t err_usart2[4];
volatile uint32_t lse_ok;

static void gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af, uint32_t pull_up)
{
    port->MODER = (port->MODER & ~(3U << (pin * 2))) | (2U << (pin * 2));
    port->OSPEEDR |= 3U << (pin * 2);
    port->PUPDR = (port->PUPDR & ~(3U << (pin * 2))) | ((pull_up ? 1U : 0U) << (pin * 2));
    volatile uint32_t *afr = &port->AFR[pin / 8];
    *afr = (*afr & ~(0xFU << ((pin % 8) * 4))) | (af << ((pin % 8) * 4));
}

static void uart_init(USART_TypeDef *u)
{
    u->CR1 = 0;
    u->BRR = (SYSCLK + BAUD / 2) / BAUD;
    u->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

// Move one byte from src UART into ring, and one byte from ring out to dst UART.
static void pump(USART_TypeDef *src, ring_t *r, USART_TypeDef *dst, volatile uint32_t *err)
{
    uint32_t isr = src->ISR;
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        err[0] += (isr & USART_ISR_FE) ? 1U : 0U;
        err[1] += (isr & USART_ISR_NE) ? 1U : 0U;
        err[2] += (isr & USART_ISR_ORE) ? 1U : 0U;
        src->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    }
    if (isr & USART_ISR_RXNE) {
        uint8_t b = (uint8_t)src->RDR;
        uint16_t next = (uint16_t)((r->head + 1U) % RING_LEN);
        if (next != r->tail) {
            r->buf[r->head] = b;
            r->head = next;
        } else {
            err[3]++;
        }
    }
    if (r->tail != r->head && (dst->ISR & USART_ISR_TXE)) {
        dst->TDR = r->buf[r->tail];
        r->tail = (uint16_t)((r->tail + 1U) % RING_LEN);
    }
}

int main(void)
{
    // MSI 4 MHz -> 48 MHz (range 11); needs 2 flash wait states first
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_2WS;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_2WS) {
    }
    RCC->CR = (RCC->CR & ~RCC_CR_MSIRANGE) | RCC_CR_MSIRANGE_11 | RCC_CR_MSIRGSEL;
    while (!(RCC->CR & RCC_CR_MSIRDY)) {
    }

    // Trim MSI against the 32.768 kHz LSE crystal (MSI PLL mode) so the baud rate is accurate
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    PWR->CR1 |= PWR_CR1_DBP;
    RCC->BDCR |= RCC_BDCR_LSEON;
    for (uint32_t i = 0; i < 20000000U && !(RCC->BDCR & RCC_BDCR_LSERDY); i++) {
    }
    if (RCC->BDCR & RCC_BDCR_LSERDY) {
        RCC->CR |= RCC_CR_MSIPLLEN;
        lse_ok = 1;
    }

    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    (void)RCC->APB1ENR1;

    gpio_af(GPIOA, 2, 7, 0);   // USART2_TX -> ST-LINK VCP
    gpio_af(GPIOA, 15, 3, 1);  // USART2_RX <- ST-LINK VCP
    gpio_af(GPIOB, 6, 7, 0);   // USART1_TX -> ESP RX
    gpio_af(GPIOB, 7, 7, 1);   // USART1_RX <- ESP TX

    uart_init(USART1);
    uart_init(USART2);

    while (1) {
        pump(USART2, &to_esp, USART1, err_usart2);
        pump(USART1, &to_pc, USART2, err_usart1);
    }
}
