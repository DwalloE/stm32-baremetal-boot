/*
 * clocktree.c - see clocktree.h. Pure C, no registers, host-testable.
 */
#include "clocktree.h"

uint8_t clk_flash_latency(uint32_t sysclk_hz)
{
    /*
     * PM0075 §3.1 "Reading the flash memory", FLASH_ACR.LATENCY:
     *   000: zero wait state,  0 < SYSCLK <= 24 MHz
     *   001: one wait state,  24 < SYSCLK <= 48 MHz
     *   010: two wait states, 48 < SYSCLK <= 72 MHz
     */
    if (sysclk_hz <= 24000000u) {
        return 0;
    }
    if (sysclk_hz <= 48000000u) {
        return 1;
    }
    return 2;
}

/* Try one PLL input frequency; fill *out and return 1 if an integer 2..16 multiplier lands exactly. */
static int try_pll_input(uint32_t in_hz, uint32_t target_hz, uint8_t *mul_out)
{
    uint32_t mul;

    if ((target_hz % in_hz) != 0u) {
        return 0;
    }
    mul = target_hz / in_hz;
    /*
     * Only the upper bound is checked. The lower bound (x2) cannot be
     * violated by the time we get here: the caller has already required
     * target >= 16 MHz (PLL output floor) and the largest PLL input is
     * 16 MHz (HSE direct), and target == HSE is served without the PLL, so
     * mul is always >= 2. The branch gate refused the dead guard.
     */
    if (mul > CLK_PLLMUL_MAX) {
        return 0;
    }
    *mul_out = (uint8_t)mul;
    return 1;
}

static void fill_buses(clk_plan_t *p, uint32_t sysclk_hz)
{
    p->sysclk_hz = sysclk_hz;
    p->hclk_hz   = sysclk_hz;            /* AHB prescaler /1: RCC_CFGR.HPRE = 0 */
    p->pclk2_hz  = sysclk_hz;            /* APB2 prescaler /1: PPRE2 = 0, max 72 MHz */
    if (sysclk_hz > CLK_PCLK1_MAX_HZ) {  /* APB1 is limited to 36 MHz (RM0008 §7.3.2 PPRE1 note) */
        p->ppre1_bits = 0x4u;            /* 0b100 = HCLK/2 */
        p->pclk1_hz   = sysclk_hz / 2u;
    } else {
        p->ppre1_bits = 0x0u;
        p->pclk1_hz   = sysclk_hz;
    }
    p->flash_latency = clk_flash_latency(sysclk_hz);
}

clk_err_t clk_plan(clk_source_t src, uint32_t src_hz, uint32_t target_hz,
                   clk_plan_t *out)
{
    uint32_t osc_hz;
    uint8_t  mul = 0;

    out->use_pll = 0;
    out->pllsrc = 0;
    out->pllxtpre = 0;
    out->pllmul = 0;
    out->pllmul_bits = 0;

    if (target_hz == 0u) {
        return CLK_ERR_TARGET_ZERO;
    }
    if (target_hz > CLK_SYSCLK_MAX_HZ) {
        return CLK_ERR_TARGET_TOO_HIGH;
    }

    if (src == CLK_SRC_HSE) {
        if (src_hz < CLK_HSE_MIN_HZ || src_hz > CLK_HSE_MAX_HZ) {
            return CLK_ERR_HSE_OUT_OF_RANGE;
        }
        osc_hz = src_hz;
    } else {
        osc_hz = CLK_HSI_HZ;             /* the argument is ignored: HSI is 8 MHz, period */
    }

    /* Running straight off the oscillator: SW = HSI/HSE, PLL stays off. */
    if (target_hz == osc_hz) {
        fill_buses(out, target_hz);
        return CLK_OK;
    }

    /* Anything else needs the PLL, whose output must sit in 16..72 MHz. */
    if (target_hz < CLK_PLL_OUT_MIN_HZ) {
        return CLK_ERR_PLL_OUT_TOO_LOW;
    }

    if (src == CLK_SRC_HSI) {
        /* RM0008 Figure 8: the HSI feeds the PLL through a fixed /2. 4 MHz x 16 = 64 MHz. */
        if (target_hz > (CLK_HSI_HZ / 2u) * CLK_PLLMUL_MAX) {
            return CLK_ERR_HSI_PATH_LIMIT;
        }
        if (!try_pll_input(CLK_HSI_HZ / 2u, target_hz, &mul)) {
            return CLK_ERR_NOT_INTEGER_MULTIPLE;
        }
        out->pllsrc = 0;
        out->pllxtpre = 0;
    } else {
        /* HSE straight in first; then HSE/2 via PLLXTPRE (buys odd ratios, e.g. 8 MHz -> 36 MHz). */
        if (try_pll_input(osc_hz, target_hz, &mul)) {
            out->pllxtpre = 0;
        } else if (try_pll_input(osc_hz / 2u, target_hz, &mul)) {
            out->pllxtpre = 1;
        } else {
            return CLK_ERR_NOT_INTEGER_MULTIPLE;
        }
        out->pllsrc = 1;
    }

    out->use_pll = 1;
    out->pllmul = mul;
    out->pllmul_bits = (uint8_t)(mul - 2u);  /* RCC_CFGR.PLLMUL: 0000 = x2 ... 1110 = x16 */
    fill_buses(out, target_hz);
    return CLK_OK;
}

uint32_t clk_usart_brr(uint32_t pclk_hz, uint32_t baud)
{
    uint32_t brr;

    if (baud == 0u) {
        return 0;
    }
    /* round(pclk / baud), computed as (2*pclk + baud) / (2*baud); 2*pclk fits: max 144e6 */
    brr = (2u * pclk_hz + baud) / (2u * baud);
    if (brr < 16u || brr > 0xFFFFu) {   /* USARTDIV below 1.0 or above the 16-bit register */
        return 0;
    }
    return brr;
}

uint32_t clk_systick_reload_1ms(uint32_t hclk_hz)
{
    /* SysTick fires when the counter wraps from 0, so N cycles needs RELOAD = N-1 (PM0056 §4.5.2). */
    return hclk_hz / 1000u - 1u;
}

const char *clk_err_str(clk_err_t e)
{
    switch (e) {
    case CLK_OK:                       return "ok";
    case CLK_ERR_TARGET_ZERO:          return "target is 0 Hz";
    case CLK_ERR_TARGET_TOO_HIGH:      return "target above 72 MHz";
    case CLK_ERR_HSE_OUT_OF_RANGE:     return "HSE outside 4..16 MHz";
    case CLK_ERR_HSI_PATH_LIMIT:       return "HSI/2 x 16 caps at 64 MHz";
    case CLK_ERR_PLL_OUT_TOO_LOW:      return "PLL output below 16 MHz";
    case CLK_ERR_NOT_INTEGER_MULTIPLE: return "no PLLMUL 2..16 hits it exactly";
    default:                           return "?";
    }
}
