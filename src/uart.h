#ifndef UART_H
#define UART_H

#include <stdint.h>

/* USART1 on PA9/PA10, 8N1, polling. brr is the USART_BRR value (clk_usart_brr). */
void uart_init(uint32_t brr);
void uart_putc(uint8_t c);
/* Non-blocking: returns the byte, or -1 if RXNE is clear. */
int uart_getc(void);

#endif
