/*
 * regs.h - every memory-mapped register this firmware touches, by name.
 *
 * No CMSIS, no HAL: each address and bit is written out with the manual
 * section and page it comes from, so a reader can check the number against
 * the page instead of trusting a header. Page numbers are for these exact
 * revisions (the ones read while writing this):
 *
 *   RM0008 Rev 15 (June 2014, DocID13902) STM32F101xx/102xx/103xx/105xx/107xx reference manual
 *   PM0056 Rev 5  (May 2013, DocID15491)  STM32F10xxx/20xxx/21xxx/L1xxxx Cortex-M3 programming manual
 *   ARMv7-M ARM   (ARM DDI 0403E.e)        Architecture Reference Manual
 *
 * Only the registers used are listed; a register not needed to boot is not
 * here, deliberately.
 */
#ifndef REGS_H
#define REGS_H

#include <stdint.h>

/* A volatile access is the whole point: the compiler must not cache or fold these. */
#define REG32(addr)   (*(volatile uint32_t *)(addr))

/* ------------------------------------------------------------------------ */
/* RCC - Reset and clock control     RM0008 §7.3 p.99, base 0x4002 1000      */
/*                                   (§3.3 Table 3 memory map, p.51)         */
/* ------------------------------------------------------------------------ */
#define RCC_BASE        0x40021000u
#define RCC_CR          REG32(RCC_BASE + 0x00u)   /* §7.3.1 p.99  Clock control register */
#define RCC_CFGR        REG32(RCC_BASE + 0x04u)   /* §7.3.2 p.101 Clock configuration register */
#define RCC_APB2ENR     REG32(RCC_BASE + 0x18u)   /* §7.3.7 p.112 APB2 peripheral clock enable */

/* RCC_CR bits (§7.3.1 pp.99-100) */
#define RCC_CR_HSION    (1u << 0)     /* internal 8 MHz RC on (reset value: 1) */
#define RCC_CR_HSIRDY   (1u << 1)     /* HSI stable - read only */
#define RCC_CR_HSEON    (1u << 16)    /* external crystal oscillator on */
#define RCC_CR_HSERDY   (1u << 17)    /* HSE stable - read only, set by hardware */
#define RCC_CR_PLLON    (1u << 24)
#define RCC_CR_PLLRDY   (1u << 25)    /* PLL locked - read only */

/* RCC_CFGR fields (§7.3.2 pp.101-103) */
#define RCC_CFGR_SW_SHIFT       0u    /* SW[1:0]   system clock switch: 00 HSI, 01 HSE, 10 PLL */
#define RCC_CFGR_SW_MASK        (0x3u << RCC_CFGR_SW_SHIFT)
#define RCC_CFGR_SWS_SHIFT      2u    /* SWS[3:2]  which clock IS the system clock - read only */
#define RCC_CFGR_SWS_MASK       (0x3u << RCC_CFGR_SWS_SHIFT)
#define RCC_CFGR_SW_HSI         0x0u
#define RCC_CFGR_SW_HSE         0x1u
#define RCC_CFGR_SW_PLL         0x2u
#define RCC_CFGR_HPRE_SHIFT     4u    /* HPRE[7:4]   AHB prescaler, 0xxx = /1 */
#define RCC_CFGR_HPRE_MASK      (0xFu << RCC_CFGR_HPRE_SHIFT)
#define RCC_CFGR_PPRE1_SHIFT    8u    /* PPRE1[10:8] APB1 prescaler, 100 = /2; APB1 max 36 MHz (§7.2 p.93) */
#define RCC_CFGR_PPRE1_MASK     (0x7u << RCC_CFGR_PPRE1_SHIFT)
#define RCC_CFGR_PPRE2_SHIFT    11u   /* PPRE2[13:11] APB2 prescaler, 0xx = /1 */
#define RCC_CFGR_PPRE2_MASK     (0x7u << RCC_CFGR_PPRE2_SHIFT)
#define RCC_CFGR_ADCPRE_SHIFT   14u   /* ADCPRE[15:14] ADC prescaler (ADC max 14 MHz) - left /2 default, ADC unused */
#define RCC_CFGR_PLLSRC         (1u << 16)  /* 0: HSI/2 feeds the PLL, 1: HSE (through PLLXTPRE) */
#define RCC_CFGR_PLLXTPRE       (1u << 17)  /* 1: HSE divided by 2 before the PLL */
#define RCC_CFGR_PLLMUL_SHIFT   18u   /* PLLMUL[21:18] p.102: 0000 = x2 ... 0111 = x9 ... 1110 = x16 */
#define RCC_CFGR_PLLMUL_MASK    (0xFu << RCC_CFGR_PLLMUL_SHIFT)

/* RCC_APB2ENR bits (§7.3.7 pp.112-113) */
#define RCC_APB2ENR_AFIOEN      (1u << 0)
#define RCC_APB2ENR_IOPAEN      (1u << 2)
#define RCC_APB2ENR_IOPCEN      (1u << 4)
#define RCC_APB2ENR_USART1EN    (1u << 14)

/* ------------------------------------------------------------------------ */
/* FLASH interface                    RM0008 §3.3.3 p.61, base 0x4002 2000   */
/* ------------------------------------------------------------------------ */
#define FLASH_ACR       REG32(0x40022000u)        /* Flash access control register, reset value 0x0000 0030 */
#define FLASH_ACR_LATENCY_MASK  0x7u              /* LATENCY[2:0]: 000 = 0 WS (0 < SYSCLK <= 24 MHz), 001 = 1 WS (<= 48), 010 = 2 WS (<= 72) */
#define FLASH_ACR_PRFTBE        (1u << 4)         /* prefetch buffer enable (reset value: 1) */
#define FLASH_ACR_PRFTBS        (1u << 5)         /* prefetch buffer status - read only */

/* ------------------------------------------------------------------------ */
/* GPIO                               RM0008 §9.2 pp.171-173                 */
/* ------------------------------------------------------------------------ */
#define GPIOA_BASE      0x40010800u   /* §3.3 Table 3 memory map, p.51 */
#define GPIOC_BASE      0x40011000u
#define GPIO_CRL(base)  REG32((base) + 0x00u)     /* §9.2.1 p.171 port configuration low,  pins 0-7  */
#define GPIO_CRH(base)  REG32((base) + 0x04u)     /* §9.2.2 p.172 port configuration high, pins 8-15 */
#define GPIO_IDR(base)  REG32((base) + 0x08u)     /* §9.2.3 p.172 input data */
#define GPIO_ODR(base)  REG32((base) + 0x0Cu)     /* §9.2.4 p.173 output data */
#define GPIO_BSRR(base) REG32((base) + 0x10u)     /* §9.2.5 p.173 bit set/reset: atomic, no read-modify-write */
/*
 * Each pin has a 4-bit CNF[1:0]:MODE[1:0] nibble in CRL/CRH (§9.2.1 p.171):
 *   MODE 00 input, 01 output 10 MHz, 10 output 2 MHz, 11 output 50 MHz
 *   CNF (output) 00 general-purpose push-pull, 01 GP open-drain, 10 alternate-function push-pull, 11 AF open-drain
 *   CNF (input)  00 analog, 01 floating, 10 pull-up/pull-down (reset value 0x4444 4444 = all floating inputs)
 */
#define GPIO_CNF_MODE_OUT_PP_2MHZ   0x2u  /* CNF 00, MODE 10 */
#define GPIO_CNF_MODE_AF_PP_50MHZ   0xBu  /* CNF 10, MODE 11 */
#define GPIO_CNF_MODE_IN_FLOATING   0x4u  /* CNF 01, MODE 00 */
#define GPIO_PIN_NIBBLE_SHIFT(pin)  (((pin) & 7u) * 4u)

/* ------------------------------------------------------------------------ */
/* USART1                             RM0008 §27.6 pp.811-814, base 0x4001 3800 */
/* ------------------------------------------------------------------------ */
#define USART1_BASE     0x40013800u
#define USART1_SR       REG32(USART1_BASE + 0x00u) /* §27.6.1 p.811 status */
#define USART1_DR       REG32(USART1_BASE + 0x04u) /* §27.6.2 p.813 data: write transmits, read receives */
#define USART1_BRR      REG32(USART1_BASE + 0x08u) /* §27.6.3 p.813 baud rate: DIV_Mantissa[15:4], DIV_Fraction[3:0] */
#define USART1_CR1      REG32(USART1_BASE + 0x0Cu) /* §27.6.4 p.814 control 1 */
#define USART_SR_RXNE   (1u << 5)     /* read data register not empty */
#define USART_SR_TC     (1u << 6)     /* transmission complete */
#define USART_SR_TXE    (1u << 7)     /* transmit data register empty */
#define USART_CR1_RE    (1u << 2)
#define USART_CR1_TE    (1u << 3)
#define USART_CR1_UE    (1u << 13)    /* USART enable */
/* USART1 pins: PA9 = TX, PA10 = RX with USART1_REMAP = 0 (§9.3.8 Table 54, p.181) */
#define USART1_TX_PIN   9u
#define USART1_RX_PIN   10u

/* ------------------------------------------------------------------------ */
/* SysTick                            PM0056 §4.5 pp.150-153, base 0xE000 E010 */
/* ------------------------------------------------------------------------ */
#define STK_CTRL        REG32(0xE000E010u)        /* §4.5.1 p.151 control and status */
#define STK_LOAD        REG32(0xE000E014u)        /* §4.5.2 p.152 reload value, 24 bits */
#define STK_VAL         REG32(0xE000E018u)        /* §4.5.3 p.153 current value: any write clears it */
#define STK_CTRL_ENABLE     (1u << 0)
#define STK_CTRL_TICKINT    (1u << 1)             /* count-to-zero raises the SysTick exception */
#define STK_CTRL_CLKSOURCE  (1u << 2)             /* 1 = processor clock (AHB), 0 = AHB/8 */

/* ------------------------------------------------------------------------ */
/* SCB - System control block         PM0056 §4.4 p.129, base 0xE000 ED00    */
/* ------------------------------------------------------------------------ */
#define SCB_VTOR        REG32(0xE000ED08u)        /* §4.4.4  p.133 vector table offset (reset: 0x0000_0000) */
#define SCB_CFSR        REG32(0xE000ED28u)        /* §4.4.10 p.142 configurable fault status = MMFSR | BFSR<<8 | UFSR<<16 */
#define SCB_HFSR        REG32(0xE000ED2Cu)        /* §4.4.11 p.145 hard fault status */
#define SCB_BFAR        REG32(0xE000ED38u)        /* §4.4.13 p.147 bus fault address */
/* CFSR bits: UsageFault status in [31:16] (§4.4.10 p.143) */
#define CFSR_UNDEFINSTR (1u << 16)
#define CFSR_INVSTATE   (1u << 17)                /* EPSR.T = 0: tried to execute with the Thumb bit clear */
#define CFSR_INVPC      (1u << 18)
#define CFSR_NOCP       (1u << 19)
#define CFSR_UNALIGNED  (1u << 24)
#define CFSR_DIVBYZERO  (1u << 25)
/* BusFault status in [15:8] (§4.4.10 p.144) */
#define CFSR_IBUSERR    (1u << 8)
#define CFSR_PRECISERR  (1u << 9)
#define CFSR_IMPRECISERR (1u << 10)
#define CFSR_BFARVALID  (1u << 15)
/* HFSR bits (§4.4.11 p.145) */
#define HFSR_VECTTBL    (1u << 1)                 /* bus fault on a vector table read */
#define HFSR_FORCED     (1u << 30)                /* a configurable fault escalated because its handler was disabled */

#endif
