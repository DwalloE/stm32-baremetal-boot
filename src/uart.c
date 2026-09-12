/*
 * uart.c - USART1 by register, polling only. Project 07 makes it
 * interrupt-driven; this one just needs a wire to print verdicts on.
 *
 * RM0008 Rev 15 §27.3.2 "Transmitter" (p.784): enable UE, program BRR, set TE,
 * then write DR whenever TXE says the shift register took the previous byte.
 * Register pages: §27.6 pp.811-814.
 */
#include "uart.h"
#include "regs.h"

void uart_init(uint32_t brr)
{
    /*
     * Peripheral clocks first (§7.3.7): a register write to an unclocked
     * peripheral is silently dropped on real silicon. USART1, port A and
     * AFIO all hang off APB2.
     */
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    /*
     * PA9  = TX: alternate-function push-pull, 50 MHz slew (nibble 1011)
     * PA10 = RX: floating input (nibble 0100), which is also the reset value.
     * Both are pins 8-15, so they live in CRH (§9.2.2 p.172). Pin assignment: Table 54 p.181.
     */
    {
        uint32_t crh = GPIO_CRH(GPIOA_BASE);
        crh &= ~(0xFu << GPIO_PIN_NIBBLE_SHIFT(USART1_TX_PIN));
        crh |=  (GPIO_CNF_MODE_AF_PP_50MHZ << GPIO_PIN_NIBBLE_SHIFT(USART1_TX_PIN));
        crh &= ~(0xFu << GPIO_PIN_NIBBLE_SHIFT(USART1_RX_PIN));
        crh |=  (GPIO_CNF_MODE_IN_FLOATING << GPIO_PIN_NIBBLE_SHIFT(USART1_RX_PIN));
        GPIO_CRH(GPIOA_BASE) = crh;
    }

    USART1_CR1 = 0;                     /* known state: 8 data bits, no parity, disabled */
    USART1_BRR = brr;                   /* §27.6.3, computed from PCLK2 by clk_usart_brr() */
    USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_putc(uint8_t c)
{
    while ((USART1_SR & USART_SR_TXE) == 0u) {
        /* wait for the transmit data register to drain into the shift register */
    }
    USART1_DR = c;
}

int uart_getc(void)
{
    if ((USART1_SR & USART_SR_RXNE) == 0u) {
        return -1;
    }
    return (int)(USART1_DR & 0xFFu);   /* reading DR clears RXNE (§27.6.1) */
}
