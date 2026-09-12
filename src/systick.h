#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

/* reload = HCLK cycles per tick minus one (clk_systick_reload_1ms). */
void systick_init(uint32_t reload);
/* Milliseconds since systick_init, from the SysTick exception. */
uint32_t systick_ms(void);

#endif
