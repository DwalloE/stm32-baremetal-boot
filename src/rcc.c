/*
 * rcc.c - applying the clock plan by register. RM0008 Rev 15 §7.2 (p.93), §7.3 (p.99), §3.3.3 (p.61).
 *
 * Sequence (the order matters and each step says why):
 *   1. flash wait states UP before the clock goes up - fetching at 72 MHz
 *      with 0 WS reads garbage
 *   2. HSE on, wait ready
 *   3. PLL source/multiplier and bus prescalers in CFGR while the PLL is OFF
 *      (PLLSRC/PLLXTPRE/PLLMUL are writable only when PLL is disabled, §7.3.2)
 *   4. PLL on, wait locked
 *   5. switch SW to PLL, wait until SWS says the switch happened
 *
 * Every wait is bounded. Real silicon sets these bits in microseconds; a
 * simulator that does not model a bit never will, and a firmware that spins
 * forever on it tells nobody anything. The report records what was seen and
 * rcc_print_verdict() prints the truth either way.
 */
#include "rcc.h"
#include "regs.h"
#include "fmt.h"

static uint32_t wait_set(volatile uint32_t *reg, uint32_t mask, uint8_t *ok)
{
    uint32_t polls = 0;

    while (polls < RCC_WAIT_POLLS) {
        polls++;
        if ((*reg & mask) == mask) {
            *ok = 1;
            return polls;
        }
    }
    *ok = 0;
    return polls;
}

void rcc_apply(const clk_plan_t *plan, rcc_report_t *rep)
{
    uint32_t cfgr;
    uint32_t sw;

    rep->hse_ready = 1;    /* "not needed" counts as satisfied; overwritten if we do need it */
    rep->pll_ready = 1;
    rep->sws_ok = 0;
    rep->polls_hse = rep->polls_pll = rep->polls_sws = 0;

    /* 1. Wait states for the TARGET frequency, prefetch on (RM0008 §3.3.3 p.61). */
    FLASH_ACR = FLASH_ACR_PRFTBE | (plan->flash_latency & FLASH_ACR_LATENCY_MASK);

    /* 2. Crystal, if the plan uses it (as PLL source or directly). */
    if ((plan->use_pll && plan->pllsrc) || (!plan->use_pll && plan->sysclk_hz != CLK_HSI_HZ)) {
        RCC_CR |= RCC_CR_HSEON;
        rep->polls_hse = wait_set(&RCC_CR, RCC_CR_HSERDY, &rep->hse_ready);
    }

    /* 3. CFGR: prescalers and PLL configuration, PLL still off. */
    cfgr  = RCC_CFGR;
    cfgr &= ~(RCC_CFGR_HPRE_MASK | RCC_CFGR_PPRE1_MASK | RCC_CFGR_PPRE2_MASK |
              RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMUL_MASK);
    cfgr |= ((uint32_t)plan->ppre1_bits << RCC_CFGR_PPRE1_SHIFT);   /* HPRE = PPRE2 = 0: /1 */
    if (plan->use_pll) {
        cfgr |= ((uint32_t)plan->pllmul_bits << RCC_CFGR_PLLMUL_SHIFT);
        if (plan->pllsrc) {
            cfgr |= RCC_CFGR_PLLSRC;
        }
        if (plan->pllxtpre) {
            cfgr |= RCC_CFGR_PLLXTPRE;
        }
    }
    RCC_CFGR = cfgr;

    /* 4. PLL on, wait for lock. */
    if (plan->use_pll) {
        RCC_CR |= RCC_CR_PLLON;
        rep->polls_pll = wait_set(&RCC_CR, RCC_CR_PLLRDY, &rep->pll_ready);
        sw = RCC_CFGR_SW_PLL;
    } else {
        sw = (plan->sysclk_hz == CLK_HSI_HZ) ? RCC_CFGR_SW_HSI : RCC_CFGR_SW_HSE;
    }

    /* 5. Switch, then wait for the hardware to report that it switched. */
    cfgr = (RCC_CFGR & ~RCC_CFGR_SW_MASK) | (sw << RCC_CFGR_SW_SHIFT);
    RCC_CFGR = cfgr;
    {
        uint32_t polls = 0;
        uint32_t sws = 0;
        while (polls < RCC_WAIT_POLLS) {
            polls++;
            sws = (RCC_CFGR & RCC_CFGR_SWS_MASK) >> RCC_CFGR_SWS_SHIFT;
            if (sws == sw) {
                rep->sws_ok = 1;
                break;
            }
        }
        rep->polls_sws = polls;
        rep->sws_seen = sws;
    }

    rep->cr = RCC_CR;
    rep->cfgr = RCC_CFGR;
    rep->flash_acr = FLASH_ACR;
}

int rcc_print_verdict(const clk_plan_t *plan, const rcc_report_t *rep, clk_source_t src, uint32_t src_hz)
{
    int verified = rep->hse_ready && rep->pll_ready && rep->sws_ok;

    if (verified) {
        out_str("clk: sysclk ");
        out_dec(plan->sysclk_hz);
        out_str(" Hz via ");
        if (plan->use_pll) {
            out_str("PLL (");
            out_str(src == CLK_SRC_HSE ? "HSE " : "HSI/2 ");
            out_dec((src == CLK_SRC_HSE ? src_hz : CLK_HSI_HZ / 2u) / 1000000u);
            out_str(" MHz x");
            out_dec(plan->pllmul);
            out_str("), SWS confirms PLL");
        } else {
            out_str(src == CLK_SRC_HSE ? "HSE directly, SWS confirms HSE" : "HSI directly, SWS confirms HSI");
        }
        out_str(", flash ");
        out_dec(plan->flash_latency);
        out_str(" WS, APB1 /");
        out_dec(plan->ppre1_bits ? 2u : 1u);
        out_nl();
        return 1;
    }

    out_str("clk UNVERIFIED: hse_ready=");
    out_dec(rep->hse_ready);
    out_str(" pll_ready=");
    out_dec(rep->pll_ready);
    out_str(" sws=");
    out_hex32(rep->sws_seen);
    out_str(" (want ");
    out_hex32(plan->use_pll ? RCC_CFGR_SW_PLL : (plan->sysclk_hz == CLK_HSI_HZ ? RCC_CFGR_SW_HSI : RCC_CFGR_SW_HSE));
    out_str(") after ");
    out_dec(rep->polls_sws);
    out_str(" polls - RCC readback does not confirm the requested tree");
    out_nl();
    return 0;
}

void rcc_print_regs(void)
{
    out_str("clk: RCC_CR=");
    out_hex32(RCC_CR);
    out_str(" RCC_CFGR=");
    out_hex32(RCC_CFGR);
    out_str(" FLASH_ACR=");
    out_hex32(FLASH_ACR);
    out_str(" STK_LOAD=");
    out_hex32(STK_LOAD);
    out_nl();
}
