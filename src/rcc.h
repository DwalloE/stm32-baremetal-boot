#ifndef RCC_H
#define RCC_H

#include <stdint.h>
#include "clocktree.h"

/* How many status polls each wait is allowed before it gives up honestly. */
#define RCC_WAIT_POLLS 100000u

typedef struct {
    uint8_t  hse_ready;     /* RCC_CR.HSERDY seen (or not needed) */
    uint8_t  pll_ready;     /* RCC_CR.PLLRDY seen (or not needed) */
    uint8_t  sws_ok;        /* RCC_CFGR.SWS matched the requested SW */
    uint32_t sws_seen;      /* last SWS field read */
    uint32_t polls_hse;
    uint32_t polls_pll;
    uint32_t polls_sws;
    uint32_t cr;            /* RCC_CR after the sequence */
    uint32_t cfgr;          /* RCC_CFGR after the sequence */
    uint32_t flash_acr;     /* FLASH_ACR after the sequence */
} rcc_report_t;

/* Program the clock tree from the plan. Never hangs: every wait is bounded and reported. */
void rcc_apply(const clk_plan_t *plan, rcc_report_t *rep);

/* The firmware's own verdict line: "clk: ..." when every readback agrees, "clk UNVERIFIED: ..." otherwise. Returns 1 if verified. */
int rcc_print_verdict(const clk_plan_t *plan, const rcc_report_t *rep, clk_source_t src, uint32_t src_hz);

/* Dump CR/CFGR/ACR for the `clk` shell command. */
void rcc_print_regs(void);

#endif
