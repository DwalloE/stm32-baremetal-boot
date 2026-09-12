/*
 * clocktree.h - the STM32F103 clock-tree arithmetic, as a pure function.
 *
 * Nothing in this file touches a register. It turns "I want SYSCLK = X from
 * source Y" into the PLL multiplier, prescalers and flash wait states the
 * reference manual allows, or an error naming the rule that was broken.
 * rcc.c applies the result; test/test_clocktree.c proves every branch on
 * the host (100% branch coverage, gated in CI).
 *
 * Rules encoded (RM0008 Rev 21 §7.2 "Clocks" / Figure 8, §7.3.2 RCC_CFGR;
 * PM0075 §3.1 / RM0008 §3.3.3 flash latency; DS5319 §5.3.x oscillator ranges):
 *   - SYSCLK <= 72 MHz, HCLK = SYSCLK (AHB /1), PCLK2 = HCLK (APB2 /1),
 *     PCLK1 <= 36 MHz so APB1 is /2 whenever HCLK > 36 MHz
 *   - PLL input is HSI/2 (fixed 4 MHz) or HSE (optionally /2 via PLLXTPRE)
 *   - PLLMUL is an integer 2..16; PLL output must be 16..72 MHz
 *   - therefore the HSI path tops out at 4 MHz x 16 = 64 MHz; 72 needs HSE
 *   - HSE crystal 4..16 MHz
 *   - flash wait states: 0 for SYSCLK <= 24 MHz, 1 for <= 48 MHz, 2 for <= 72 MHz
 */
#ifndef CLOCKTREE_H
#define CLOCKTREE_H

#include <stdint.h>

#define CLK_HSI_HZ        8000000u   /* internal RC, RM0008 §7.2.2 */
#define CLK_SYSCLK_MAX_HZ 72000000u
#define CLK_PCLK1_MAX_HZ  36000000u
#define CLK_PLL_OUT_MIN_HZ 16000000u
#define CLK_HSE_MIN_HZ    4000000u
#define CLK_HSE_MAX_HZ    16000000u
#define CLK_PLLMUL_MIN    2u
#define CLK_PLLMUL_MAX    16u

typedef enum {
    CLK_SRC_HSI = 0,
    CLK_SRC_HSE = 1
} clk_source_t;

typedef enum {
    CLK_OK = 0,
    CLK_ERR_TARGET_ZERO,        /* target_hz == 0 */
    CLK_ERR_TARGET_TOO_HIGH,    /* > 72 MHz on any path */
    CLK_ERR_HSE_OUT_OF_RANGE,   /* crystal outside 4..16 MHz */
    CLK_ERR_HSI_PATH_LIMIT,     /* > 64 MHz asked from HSI: the /2 is not optional */
    CLK_ERR_PLL_OUT_TOO_LOW,    /* PLL would have to run below 16 MHz */
    CLK_ERR_NOT_INTEGER_MULTIPLE/* no PLLMUL in 2..16 (with or without XTPRE) hits the target exactly */
} clk_err_t;

typedef struct {
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pclk1_hz;
    uint32_t pclk2_hz;
    uint8_t  use_pll;       /* 0: SYSCLK = the source directly, PLL left off */
    uint8_t  pllsrc;        /* RCC_CFGR.PLLSRC: 0 = HSI/2, 1 = HSE (RM0008 §7.3.2) */
    uint8_t  pllxtpre;      /* RCC_CFGR.PLLXTPRE: 1 = HSE/2 into the PLL */
    uint8_t  pllmul;        /* the human number, 2..16 */
    uint8_t  pllmul_bits;   /* RCC_CFGR.PLLMUL field value = pllmul - 2 */
    uint8_t  ppre1_bits;    /* RCC_CFGR.PPRE1: 0b000 = /1, 0b100 = /2 */
    uint8_t  flash_latency; /* FLASH_ACR.LATENCY: 0, 1 or 2 wait states */
} clk_plan_t;

clk_err_t clk_plan(clk_source_t src, uint32_t src_hz, uint32_t target_hz,
                   clk_plan_t *out);

/* Flash wait states for a given SYSCLK (PM0075 §3.1). Pure. */
uint8_t clk_flash_latency(uint32_t sysclk_hz);

/*
 * USART_BRR for a given peripheral clock and baud rate (RM0008 §27.3.4,
 * Equation 1): USARTDIV = fPCLK / (16 x baud), stored as a 12.4 fixed-point
 * mantissa:fraction - which is just round(fPCLK / baud) in 1/16 units.
 * Returns 0 for an unrepresentable request (baud 0, DIV < 1 or > 4095.9375).
 */
uint32_t clk_usart_brr(uint32_t pclk_hz, uint32_t baud);

/* SysTick reload for a 1 ms tick from HCLK (PM0056 §4.5: counts RELOAD+1 cycles). */
uint32_t clk_systick_reload_1ms(uint32_t hclk_hz);

const char *clk_err_str(clk_err_t e);

#endif
