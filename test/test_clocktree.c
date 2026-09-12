/*
 * test_clocktree.c - the clock-tree arithmetic against the reference manual.
 *
 * Every expected value below is derived by hand from RM0008 / PM0075 in the
 * comment next to it, so a failing case says which rule the code broke, not
 * just that two numbers differ.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clocktree.h"

static int failures;
static int checks;

#define CHECK(cond, ...) do {                                             \
        checks++;                                                         \
        if (!(cond)) {                                                    \
            failures++;                                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                   \
            printf(__VA_ARGS__);                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)

static void test_bluepill_72mhz_from_hse(void)
{
    clk_plan_t p;
    clk_err_t e = clk_plan(CLK_SRC_HSE, 8000000u, 72000000u, &p);

    /* The shipped configuration: 8 MHz crystal x 9 = 72 MHz, no XTPRE. */
    CHECK(e == CLK_OK, "72 MHz from HSE 8 MHz must plan: %s", clk_err_str(e));
    CHECK(p.use_pll == 1, "PLL must be on");
    CHECK(p.pllsrc == 1, "PLLSRC must select HSE");
    CHECK(p.pllxtpre == 0, "PLLXTPRE must be off (8 x 9, not 4 x 18)");
    CHECK(p.pllmul == 9, "PLLMUL x9, got x%u", p.pllmul);
    CHECK(p.pllmul_bits == 7, "PLLMUL field 0111 = x9, got %u", p.pllmul_bits);
    CHECK(p.hclk_hz == 72000000u, "HCLK = SYSCLK");
    CHECK(p.pclk2_hz == 72000000u, "PCLK2 = HCLK (APB2 max is 72)");
    CHECK(p.pclk1_hz == 36000000u, "PCLK1 must be halved to 36 MHz, got %u", p.pclk1_hz);
    CHECK(p.ppre1_bits == 0x4, "PPRE1 = 0b100 (HCLK/2), got %u", p.ppre1_bits);
    CHECK(p.flash_latency == 2, "72 MHz needs 2 wait states, got %u", p.flash_latency);
}

static void test_64mhz_from_hsi_is_the_ceiling(void)
{
    clk_plan_t p;
    clk_err_t e = clk_plan(CLK_SRC_HSI, 0u, 64000000u, &p);

    /* HSI 8 MHz -> fixed /2 -> 4 MHz x 16 = 64 MHz. PLLMUL field 1110. */
    CHECK(e == CLK_OK, "64 MHz from HSI must plan: %s", clk_err_str(e));
    CHECK(p.pllsrc == 0 && p.pllxtpre == 0, "HSI/2 path has no XTPRE");
    CHECK(p.pllmul == 16 && p.pllmul_bits == 14, "x16 = field 1110, got x%u/%u", p.pllmul, p.pllmul_bits);
    CHECK(p.pclk1_hz == 32000000u && p.ppre1_bits == 0x4, "64 > 36 so APB1 is /2");
    CHECK(p.flash_latency == 2, "64 MHz is in the 48..72 band: 2 WS");

    /* And 72 from HSI is impossible - the /2 is not optional (RM0008 Figure 8). */
    e = clk_plan(CLK_SRC_HSI, 0u, 72000000u, &p);
    CHECK(e == CLK_ERR_HSI_PATH_LIMIT, "72 MHz from HSI must be refused as the HSI limit, got %s", clk_err_str(e));
}

static void test_flash_latency_bands(void)
{
    /* RM0008 §3.3.3 p.61: 0 WS up to and including 24 MHz, 1 WS up to 48, 2 WS up to 72. */
    CHECK(clk_flash_latency(8000000u) == 0, "8 MHz: 0 WS");
    CHECK(clk_flash_latency(24000000u) == 0, "24 MHz inclusive: 0 WS");
    CHECK(clk_flash_latency(24000001u) == 1, "just over 24: 1 WS");
    CHECK(clk_flash_latency(48000000u) == 1, "48 MHz inclusive: 1 WS");
    CHECK(clk_flash_latency(48000001u) == 2, "just over 48: 2 WS");
    CHECK(clk_flash_latency(72000000u) == 2, "72 MHz: 2 WS");
}

static void test_no_pll_when_target_is_the_oscillator(void)
{
    clk_plan_t p;

    /* Reset state: SYSCLK = HSI = 8 MHz. No PLL, APB1 /1, 0 WS. */
    clk_err_t e = clk_plan(CLK_SRC_HSI, 0u, 8000000u, &p);
    CHECK(e == CLK_OK && p.use_pll == 0, "8 MHz from HSI is the oscillator itself");
    CHECK(p.ppre1_bits == 0 && p.pclk1_hz == 8000000u, "APB1 stays /1 under 36 MHz");
    CHECK(p.flash_latency == 0, "0 WS at 8 MHz");

    /* Same with HSE = target. */
    e = clk_plan(CLK_SRC_HSE, 16000000u, 16000000u, &p);
    CHECK(e == CLK_OK && p.use_pll == 0 && p.pllsrc == 0, "16 MHz from a 16 MHz HSE: no PLL");
}

static void test_hse_xtpre_buys_odd_ratios(void)
{
    clk_plan_t p;

    /* 8 MHz -> 36 MHz: x4.5 is not an integer, but (8/2) x 9 is. PLLXTPRE = 1. */
    clk_err_t e = clk_plan(CLK_SRC_HSE, 8000000u, 36000000u, &p);
    CHECK(e == CLK_OK, "36 MHz from 8 MHz must plan via XTPRE: %s", clk_err_str(e));
    CHECK(p.pllxtpre == 1 && p.pllmul == 9, "expected HSE/2 x 9, got xtpre=%u mul=%u", p.pllxtpre, p.pllmul);
    CHECK(p.pclk1_hz == 36000000u && p.ppre1_bits == 0, "36 MHz exactly does NOT need APB1 /2");
    CHECK(p.flash_latency == 1, "36 MHz: 1 WS");

    /* 8 MHz -> 48 MHz: 8 x 6 works directly, XTPRE must stay 0. */
    e = clk_plan(CLK_SRC_HSE, 8000000u, 48000000u, &p);
    CHECK(e == CLK_OK && p.pllxtpre == 0 && p.pllmul == 6, "48 = 8 x 6 straight in");
}

static void test_rejections(void)
{
    clk_plan_t p;

    CHECK(clk_plan(CLK_SRC_HSE, 8000000u, 0u, &p) == CLK_ERR_TARGET_ZERO, "0 Hz");
    CHECK(clk_plan(CLK_SRC_HSE, 8000000u, 80000000u, &p) == CLK_ERR_TARGET_TOO_HIGH, "80 MHz > 72");
    CHECK(clk_plan(CLK_SRC_HSE, 2000000u, 16000000u, &p) == CLK_ERR_HSE_OUT_OF_RANGE, "2 MHz crystal too slow");
    CHECK(clk_plan(CLK_SRC_HSE, 25000000u, 25000000u, &p) == CLK_ERR_HSE_OUT_OF_RANGE, "25 MHz crystal too fast");
    CHECK(clk_plan(CLK_SRC_HSI, 0u, 12000000u, &p) == CLK_ERR_PLL_OUT_TOO_LOW, "12 MHz would need the PLL below 16 MHz");
    CHECK(clk_plan(CLK_SRC_HSI, 0u, 18000000u, &p) == CLK_ERR_NOT_INTEGER_MULTIPLE, "18 MHz is 4 x 4.5");
    CHECK(clk_plan(CLK_SRC_HSE, 8000000u, 70000000u, &p) == CLK_ERR_NOT_INTEGER_MULTIPLE, "70 MHz is neither 8 x n nor 4 x n");
    /* HSE 16 MHz -> 16 x 1 is below PLLMUL 2, XTPRE path 8 x 2 = 16 - but 16 == osc, so no PLL at all. */
    CHECK(clk_plan(CLK_SRC_HSE, 16000000u, 16000000u, &p) == CLK_OK && p.use_pll == 0, "16 from 16 is direct");
    /* HSE 12 MHz -> 18 MHz: 12 x 1.5 no; 6 x 3 yes via XTPRE. */
    CHECK(clk_plan(CLK_SRC_HSE, 12000000u, 18000000u, &p) == CLK_OK && p.pllxtpre == 1 && p.pllmul == 3, "18 from 12 via XTPRE x3");
    /* HSE 4 MHz -> 72 MHz would be x18: above PLLMUL 16 on both paths. */
    CHECK(clk_plan(CLK_SRC_HSE, 4000000u, 72000000u, &p) == CLK_ERR_NOT_INTEGER_MULTIPLE, "4 x 18 exceeds PLLMUL 16");
}

static void test_usart_brr(void)
{
    /* RM0008 §27.3.4 worked example style: 72 MHz / 115200 = 625.0 -> mantissa 39, fraction 0x1 = 0x271. */
    CHECK(clk_usart_brr(72000000u, 115200u) == 0x271u, "72 MHz @115200 -> 0x271, got 0x%x", clk_usart_brr(72000000u, 115200u));
    /* 64 MHz / 115200 = 555.55 -> 556 = 34 + 12/16 = 0x22C. */
    CHECK(clk_usart_brr(64000000u, 115200u) == 0x22Cu, "64 MHz @115200 -> 0x22C, got 0x%x", clk_usart_brr(64000000u, 115200u));
    /* 8 MHz / 9600 = 833.33 -> 833 = 52 + 1/16 = 0x341 (round down). */
    CHECK(clk_usart_brr(8000000u, 9600u) == 0x341u, "8 MHz @9600 -> 0x341, got 0x%x", clk_usart_brr(8000000u, 9600u));
    /* Rounding up: 36 MHz / 230400 = 156.25 -> 156 = 0x9C; 36 MHz / 460800 = 78.125 -> 78 = 0x4E */
    CHECK(clk_usart_brr(36000000u, 460800u) == 0x4Eu, "36 MHz @460800 -> 0x4E");
    /* Rejections: baud 0; USARTDIV below 1 (8 MHz at 1 Mbaud = 8 < 16); above 16 bits (72 MHz at 300 baud = 240000). */
    CHECK(clk_usart_brr(72000000u, 0u) == 0u, "baud 0 rejected");
    CHECK(clk_usart_brr(8000000u, 1000000u) == 0u, "USARTDIV < 1 rejected");
    CHECK(clk_usart_brr(72000000u, 300u) == 0u, "BRR > 0xFFFF rejected");
}

static void test_systick_reload(void)
{
    /* 72 MHz: 72000 cycles per ms -> RELOAD 71999. 8 MHz -> 7999. */
    CHECK(clk_systick_reload_1ms(72000000u) == 71999u, "72 MHz -> 71999");
    CHECK(clk_systick_reload_1ms(8000000u) == 7999u, "8 MHz -> 7999");
}

static void test_error_strings(void)
{
    CHECK(strcmp(clk_err_str(CLK_OK), "ok") == 0, "ok string");
    CHECK(strcmp(clk_err_str(CLK_ERR_TARGET_ZERO), "target is 0 Hz") == 0, "zero string");
    CHECK(strcmp(clk_err_str(CLK_ERR_TARGET_TOO_HIGH), "target above 72 MHz") == 0, "high string");
    CHECK(strcmp(clk_err_str(CLK_ERR_HSE_OUT_OF_RANGE), "HSE outside 4..16 MHz") == 0, "hse string");
    CHECK(strcmp(clk_err_str(CLK_ERR_HSI_PATH_LIMIT), "HSI/2 x 16 caps at 64 MHz") == 0, "hsi string");
    CHECK(strcmp(clk_err_str(CLK_ERR_PLL_OUT_TOO_LOW), "PLL output below 16 MHz") == 0, "low string");
    CHECK(strcmp(clk_err_str(CLK_ERR_NOT_INTEGER_MULTIPLE), "no PLLMUL 2..16 hits it exactly") == 0, "mul string");
    CHECK(strcmp(clk_err_str((clk_err_t)99), "?") == 0, "unknown string");
}

int main(void)
{
    test_bluepill_72mhz_from_hse();
    test_64mhz_from_hsi_is_the_ceiling();
    test_flash_latency_bands();
    test_no_pll_when_target_is_the_oscillator();
    test_hse_xtpre_buys_odd_ratios();
    test_rejections();
    test_usart_brr();
    test_systick_reload();
    test_error_strings();

    printf("clocktree: %d checks, %d failures\n", checks, failures);
    if (failures) {
        return EXIT_FAILURE;
    }
    printf("clocktree: ALL PASS\n");
    return EXIT_SUCCESS;
}
