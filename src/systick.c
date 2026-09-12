/*
 * systick.c - the 24-bit core timer, PM0056 Rev 5 §4.5 pp.150-153. The handler below REPLACES
 * the weak SysTick_Handler alias in startup.s at link time - which is the
 * whole vector-table mechanism working: the hardware reads entry 15 and
 * lands here, with no registration call anywhere.
 */
#include "systick.h"
#include "regs.h"

static volatile uint32_t g_ms;   /* .bss: zero after the startup loop, and the uptime proves it */

void SysTick_Handler(void)
{
    g_ms++;
}

void systick_init(uint32_t reload)
{
    STK_LOAD = reload & 0x00FFFFFFu;     /* 24-bit counter (§4.5.2) */
    STK_VAL  = 0;                        /* any write clears the current value and COUNTFLAG */
    STK_CTRL = STK_CTRL_CLKSOURCE | STK_CTRL_TICKINT | STK_CTRL_ENABLE;
}

uint32_t systick_ms(void)
{
    return g_ms;                         /* single 32-bit load: atomic on this core, no torn read */
}
