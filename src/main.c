/*
 * main.c - what runs once startup.s has done its five jobs.
 *
 * Order matters here too: the clock tree first (so the USART divisor is
 * computed for the bus clock we actually have), then the UART, then the
 * verdict lines, then a heartbeat LED, an uptime counter and a tiny shell.
 *
 * Every global in this file is in .bss (zero-initialized) on purpose: the
 * control build skips the .data copy, and the path to the verdict printer
 * must not depend on the thing the verdict is about.
 */
#include <stdint.h>

#include "clocktree.h"
#include "rcc.h"
#include "uart.h"
#include "gpio.h"
#include "systick.h"
#include "fmt.h"
#include "boot_check.h"
#include "fault.h"

#if defined(SYSCLK_hsi64)
#  define CLK_SRC       CLK_SRC_HSI
#  define CLK_SRC_HZ    0u
#  define CLK_TARGET_HZ 64000000u
#else /* SYSCLK_hse72, the Blue Pill's 8 MHz crystal x 9 */
#  define CLK_SRC       CLK_SRC_HSE
#  define CLK_SRC_HZ    8000000u
#  define CLK_TARGET_HZ 72000000u
#endif
#define BAUD 115200u

static char     line[32];
static uint32_t line_len;
#ifdef DEBUG_RX
/* Temporary probe: is anything driving PA10, and does RXNE ever set? */
#include "regs.h"
static uint32_t dbg_rx_low, dbg_rxne, dbg_polls;
#endif

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void print_help(void)
{
    out_str("shell: commands: help | boot | vec | clk | map | fault");
    out_nl();
}

static void dispatch(const char *cmd, const clk_plan_t *plan, const rcc_report_t *rep)
{
    if (str_eq(cmd, "help")) {
        print_help();
    } else if (str_eq(cmd, "boot")) {
        boot_check_print();
        vec_check_print();
    } else if (str_eq(cmd, "vec")) {
        boot_print_vectors();
    } else if (str_eq(cmd, "clk")) {
        rcc_print_verdict(plan, rep, CLK_SRC, CLK_SRC_HZ);
        rcc_print_regs();
    } else if (str_eq(cmd, "map")) {
        boot_print_map();
    } else if (str_eq(cmd, "fault")) {
        out_str("fault: executing UDF #0 - HardFault_Handler must catch it");
        out_nl();
        fault_trigger_udf();
        out_str("fault NOT CAUGHT: execution continued past UDF");   /* unreachable if vector 3 works */
        out_nl();
    } else if (cmd[0] != '\0') {
        out_str("shell: unknown command '");
        out_str(cmd);
        out_str("'");
        out_nl();
    }
}

static void poll_shell(const clk_plan_t *plan, const rcc_report_t *rep)
{
    int c = uart_getc();

    if (c < 0) {
        return;
    }
    if (c == '\r' || c == '\n') {
        out_nl();                          /* echo the line end so a terminal tester sees its line */
        line[line_len] = '\0';
        dispatch(line, plan, rep);
        line_len = 0;
        return;
    }
    if (line_len < sizeof line - 1u) {
        line[line_len++] = (char)c;
        out_char((char)c);                 /* echo */
    }
}

int main(void)
{
    clk_plan_t   plan;
    rcc_report_t rep;
    clk_err_t    err;
    uint32_t     next_beat = 0, next_uptime = 1000, seconds = 0;

    err = clk_plan(CLK_SRC, CLK_SRC_HZ, CLK_TARGET_HZ, &plan);
    if (err != CLK_OK) {
        /* A build-time constant that the arithmetic rejects is a build bug; make it loud at 8 MHz. */
        clk_plan(CLK_SRC_HSI, 0u, CLK_HSI_HZ, &plan);
    }
    rcc_apply(&plan, &rep);

    uart_init(clk_usart_brr(plan.pclk2_hz, BAUD));
    gpio_led_init();
    systick_init(clk_systick_reload_1ms(plan.hclk_hz));

    out_nl();
    out_str("stm32-baremetal-boot: STM32F103C8, reset vector to main() with no HAL, no CMSIS");
    out_nl();
    if (err != CLK_OK) {
        out_str("clk PLAN REJECTED: ");
        out_str(clk_err_str(err));
        out_nl();
    }

    boot_check_print();
    vec_check_print();
    rcc_print_verdict(&plan, &rep, CLK_SRC, CLK_SRC_HZ);
    print_help();

    for (;;) {
        uint32_t now = systick_ms();

        if ((int32_t)(now - next_beat) >= 0) {
            gpio_led_toggle();
            next_beat += 500;
        }
        if ((int32_t)(now - next_uptime) >= 0) {
            seconds++;
            out_str("uptime: t=");
            out_dec(seconds);
            out_str("s");
            out_nl();
#ifdef DEBUG_RX
            out_str("dbg: polls="); out_dec(dbg_polls);
            out_str(" pa10_low="); out_dec(dbg_rx_low);
            out_str(" rxne="); out_dec(dbg_rxne);
            out_str(" SR="); out_hex32(USART1_SR);
            out_str(" CR1="); out_hex32(USART1_CR1);
            out_str(" CRH="); out_hex32(GPIO_CRH(GPIOA_BASE));
            out_str(" IDR="); out_hex32(GPIO_IDR(GPIOA_BASE));
            out_nl();
#endif
            next_uptime += 1000;
        }
        poll_shell(&plan, &rep);
#ifdef DEBUG_RX
        dbg_polls++;
        if ((GPIO_IDR(GPIOA_BASE) & (1u << 10)) == 0u) dbg_rx_low++;
        if (USART1_SR & USART_SR_RXNE) dbg_rxne++;
#endif
    }
}
