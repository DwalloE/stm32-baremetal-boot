/*
 * startup.s - reset to main() on an STM32F103C8 (Cortex-M3), by hand.
 *
 * Two kinds of line live in this file and each comment says which:
 *   [ARCH]  the ARMv7-M architecture or the F103 reference manual REQUIRES it
 *   [CONV]  convention - GCC, the linker script and this project agree on it,
 *           but the silicon does not care
 *
 * References:
 *   ARMv7-M Architecture Reference Manual (DDI 0403E.e), §B1.5 "ARMv7-M
 *     exception model": vector table layout, reset behaviour, EPSR.T
 *   PM0056 Rev 5 (Cortex-M3 programming manual) §2.3.4 "Vector table" pp.35-36,
 *     §2.3.7 "Exception entry and return" p.39 (the stacked frame)
 *   RM0008 Rev 15 §10.1.2 Table 63 "Vector table for other STM32F10xxx
 *     devices" pp.203-205: the F103 medium-density external interrupt positions
 *   RM0008 Rev 15 §3.4 "Boot configuration" p.61: flash aliased at 0 when BOOT0 = 0
 *
 * Build with:  arm-none-eabi-gcc -c startup.s
 * Control build (the boot-integrity check MUST fail):
 *              arm-none-eabi-gcc -c -Wa,--defsym,CONTROL_SKIP_DATA_COPY=1 startup.s
 */

    .syntax unified             /* [CONV] one assembler syntax for ARM and Thumb */
    .cpu    cortex-m3           /* [ARCH] M3 = ARMv7-M, Thumb-2 only */
    .thumb                      /* [ARCH] Cortex-M cannot execute ARM state at all */

/* ------------------------------------------------------------------------- */
/* The vector table                                                          */
/* ------------------------------------------------------------------------- */
/*
 * [ARCH] On reset the core reads word 0 of the vector table into MSP and
 * word 1 into PC (ARMv7-M ARM §B1.5.5 "Reset behavior"). The table lives at
 * whatever VTOR points to, and VTOR resets to 0x0000_0000 - which on the
 * F103 is flash aliased from 0x0800_0000 when BOOT0 = 0 (RM0008 §3.4).
 * So the table must be the first thing in the image: the linker script puts
 * section .isr_vector first, and KEEP()s it.
 *
 * [ARCH] Bit 0 of every handler address must be 1: Cortex-M loads EPSR.T
 * from it on exception entry and a 0 there is an INVSTATE UsageFault
 * (ARMv7-M ARM §B1.5.6). The assembler sets that bit for us because each
 * handler is declared .thumb_func - it is not magic, it is that directive.
 */
    .section .isr_vector, "a", %progbits
    .type   g_vectors, %object
    .global g_vectors
g_vectors:
    .word   _estack             /* 0  [ARCH] initial MSP - top of RAM, from the linker script */
    .word   Reset_Handler       /* 1  [ARCH] initial PC, Thumb bit set by .thumb_func */
    .word   NMI_Handler         /* 2  [ARCH] fixed priority -2 */
    .word   HardFault_Handler   /* 3  [ARCH] fixed priority -1; escalation target of every disabled fault */
    .word   MemManage_Handler   /* 4  */
    .word   BusFault_Handler    /* 5  */
    .word   UsageFault_Handler  /* 6  */
    .word   0                   /* 7  [ARCH] reserved */
    .word   0                   /* 8  reserved */
    .word   0                   /* 9  reserved */
    .word   0                   /* 10 reserved */
    .word   SVC_Handler         /* 11 */
    .word   DebugMon_Handler    /* 12 */
    .word   0                   /* 13 reserved */
    .word   PendSV_Handler      /* 14 */
    .word   SysTick_Handler     /* 15 - the uptime counter's entry point */
    /*
     * [ARCH] External interrupts, positions 0..42 for the medium-density F103
     * (RM0008 Table 63). NVIC line n is table entry 16+n. Positions 43-59
     * exist only on high-density / XL / connectivity-line parts; leaving them
     * out keeps the table at exactly what this die decodes.
     */
    .word   WWDG_IRQHandler             /* 16  IRQ0  */
    .word   PVD_IRQHandler              /* 17  IRQ1  */
    .word   TAMPER_IRQHandler           /* 18  IRQ2  */
    .word   RTC_IRQHandler              /* 19  IRQ3  */
    .word   FLASH_IRQHandler            /* 20  IRQ4  */
    .word   RCC_IRQHandler              /* 21  IRQ5  */
    .word   EXTI0_IRQHandler            /* 22  IRQ6  */
    .word   EXTI1_IRQHandler            /* 23  IRQ7  */
    .word   EXTI2_IRQHandler            /* 24  IRQ8  */
    .word   EXTI3_IRQHandler            /* 25  IRQ9  */
    .word   EXTI4_IRQHandler            /* 26  IRQ10 */
    .word   DMA1_Channel1_IRQHandler    /* 27  IRQ11 */
    .word   DMA1_Channel2_IRQHandler    /* 28  IRQ12 */
    .word   DMA1_Channel3_IRQHandler    /* 29  IRQ13 */
    .word   DMA1_Channel4_IRQHandler    /* 30  IRQ14 */
    .word   DMA1_Channel5_IRQHandler    /* 31  IRQ15 */
    .word   DMA1_Channel6_IRQHandler    /* 32  IRQ16 */
    .word   DMA1_Channel7_IRQHandler    /* 33  IRQ17 */
    .word   ADC1_2_IRQHandler           /* 34  IRQ18 */
    .word   USB_HP_CAN1_TX_IRQHandler   /* 35  IRQ19 */
    .word   USB_LP_CAN1_RX0_IRQHandler  /* 36  IRQ20 */
    .word   CAN1_RX1_IRQHandler         /* 37  IRQ21 */
    .word   CAN1_SCE_IRQHandler         /* 38  IRQ22 */
    .word   EXTI9_5_IRQHandler          /* 39  IRQ23 */
    .word   TIM1_BRK_IRQHandler         /* 40  IRQ24 */
    .word   TIM1_UP_IRQHandler          /* 41  IRQ25 */
    .word   TIM1_TRG_COM_IRQHandler     /* 42  IRQ26 */
    .word   TIM1_CC_IRQHandler          /* 43  IRQ27 */
    .word   TIM2_IRQHandler             /* 44  IRQ28 */
    .word   TIM3_IRQHandler             /* 45  IRQ29 */
    .word   TIM4_IRQHandler             /* 46  IRQ30 */
    .word   I2C1_EV_IRQHandler          /* 47  IRQ31 */
    .word   I2C1_ER_IRQHandler          /* 48  IRQ32 */
    .word   I2C2_EV_IRQHandler          /* 49  IRQ33 */
    .word   I2C2_ER_IRQHandler          /* 50  IRQ34 */
    .word   SPI1_IRQHandler             /* 51  IRQ35 */
    .word   SPI2_IRQHandler             /* 52  IRQ36 */
    .word   USART1_IRQHandler           /* 53  IRQ37 - project 07 fills this one in */
    .word   USART2_IRQHandler           /* 54  IRQ38 */
    .word   USART3_IRQHandler           /* 55  IRQ39 */
    .word   EXTI15_10_IRQHandler        /* 56  IRQ40 */
    .word   RTC_Alarm_IRQHandler        /* 57  IRQ41 */
    .word   USBWakeUp_IRQHandler        /* 58  IRQ42 */
    .size   g_vectors, . - g_vectors

/* ------------------------------------------------------------------------- */
/* Reset_Handler: the first instruction this firmware executes               */
/* ------------------------------------------------------------------------- */
    .section .text.Reset_Handler, "ax", %progbits
    .thumb_func                 /* [ARCH] sets bit 0 of the symbol -> EPSR.T = 1 */
    .type   Reset_Handler, %function
    .global Reset_Handler
Reset_Handler:
    /*
     * [CONV] The hardware already loaded MSP from vector 0. Re-loading it
     * here costs one instruction and makes this entry point valid when
     * reached by a software jump (a bootloader, a debugger "run from reset
     * vector"), where nobody reloaded SP for us.
     */
    ldr     r0, =_estack
    mov     sp, r0

.ifdef CONTROL_SKIP_DATA_COPY
    /*
     * CONTROL BUILD: the .data copy is deliberately omitted. Every
     * initialized global then reads whatever RAM held at power-up (zero in
     * both simulators, random on silicon), and the boot-integrity check in
     * boot_check.c MUST print "boot INTEGRITY VIOLATION". CI requires that.
     * A safety net nobody has seen fire is decoration.
     */
.else
    /*
     * [CONV] Copy .data from its flash image (LMA, _sidata) to its RAM home
     * (VMA, _sdata .. _edata). The C standard requires initialized statics
     * to hold their initializers when main() starts; the architecture does
     * nothing to make that so - this loop is where the promise is kept.
     * Word-at-a-time is legal because the linker script aligns both ends to 4.
     */
    ldr     r0, =_sdata         /* destination cursor */
    ldr     r1, =_edata         /* destination end (exclusive) */
    ldr     r2, =_sidata        /* source cursor, in flash */
    b       .Ldata_check
.Ldata_copy:
    ldr     r3, [r2], #4        /* post-increment: load then r2 += 4 */
    str     r3, [r0], #4
.Ldata_check:
    cmp     r0, r1
    bcc     .Ldata_copy         /* unsigned lower: keep going while r0 < r1 */
.endif

    /*
     * [CONV] Zero .bss (_sbss .. _ebss). Same C-standard promise: statics
     * without an initializer start at zero. Silicon RAM does not power up
     * zeroed, and both simulators happen to - which is exactly why the boot
     * check also plants a .bss canary and checks it after this loop.
     */
    ldr     r0, =_sbss
    ldr     r1, =_ebss
    movs    r2, #0
    b       .Lbss_check
.Lbss_zero:
    str     r2, [r0], #4
.Lbss_check:
    cmp     r0, r1
    bcc     .Lbss_zero

    /*
     * [CONV] No libc means no __libc_init_array, no constructors, no
     * environment; main() takes no arguments and is not expected to return.
     * bl (not b) so that if it ever does return we land in the trap below
     * with LR pointing at it - visible in a debugger, and the fault demo's
     * decoder would name the address.
     */
    bl      main
.Lhang:
    b       .Lhang
    .size   Reset_Handler, . - Reset_Handler

/* ------------------------------------------------------------------------- */
/* HardFault_Handler: an assembly shim so C can see the stacked frame        */
/* ------------------------------------------------------------------------- */
/*
 * [ARCH] On exception entry the core pushes r0-r3, r12, lr, pc, xPSR onto
 * the active stack (ARMv7-M ARM §B1.5.6) and loads LR with EXC_RETURN.
 * Bit 2 of EXC_RETURN says which stack was in use (0 = MSP, 1 = PSP). This
 * shim hands the right stack pointer to hardfault_c(uint32_t *frame) so the
 * C code can print the faulting PC without guessing.
 */
    .section .text.HardFault_Handler, "ax", %progbits
    .thumb_func
    .type   HardFault_Handler, %function
    .global HardFault_Handler
HardFault_Handler:
    tst     lr, #4
    ite     eq
    mrseq   r0, msp
    mrsne   r0, psp
    b       hardfault_c
    .size   HardFault_Handler, . - HardFault_Handler

/* ------------------------------------------------------------------------- */
/* Default_Handler and the weak aliases                                      */
/* ------------------------------------------------------------------------- */
/*
 * [CONV] Every vector above points at a WEAK symbol aliased to this loop.
 * A C file that defines e.g. SysTick_Handler overrides the alias at link
 * time with no edit to this table. An unexpected interrupt therefore spins
 * here, where a debugger will find it, instead of jumping to address 0.
 */
    .section .text.Default_Handler, "ax", %progbits
    .thumb_func
    .type   Default_Handler, %function
    .global Default_Handler
Default_Handler:
    b       Default_Handler
    .size   Default_Handler, . - Default_Handler

    .macro  weak_alias name
    .weak   \name
    .thumb_set \name, Default_Handler
    .endm

    weak_alias NMI_Handler
    weak_alias MemManage_Handler
    weak_alias BusFault_Handler
    weak_alias UsageFault_Handler
    weak_alias SVC_Handler
    weak_alias DebugMon_Handler
    weak_alias PendSV_Handler
    weak_alias SysTick_Handler
    weak_alias WWDG_IRQHandler
    weak_alias PVD_IRQHandler
    weak_alias TAMPER_IRQHandler
    weak_alias RTC_IRQHandler
    weak_alias FLASH_IRQHandler
    weak_alias RCC_IRQHandler
    weak_alias EXTI0_IRQHandler
    weak_alias EXTI1_IRQHandler
    weak_alias EXTI2_IRQHandler
    weak_alias EXTI3_IRQHandler
    weak_alias EXTI4_IRQHandler
    weak_alias DMA1_Channel1_IRQHandler
    weak_alias DMA1_Channel2_IRQHandler
    weak_alias DMA1_Channel3_IRQHandler
    weak_alias DMA1_Channel4_IRQHandler
    weak_alias DMA1_Channel5_IRQHandler
    weak_alias DMA1_Channel6_IRQHandler
    weak_alias DMA1_Channel7_IRQHandler
    weak_alias ADC1_2_IRQHandler
    weak_alias USB_HP_CAN1_TX_IRQHandler
    weak_alias USB_LP_CAN1_RX0_IRQHandler
    weak_alias CAN1_RX1_IRQHandler
    weak_alias CAN1_SCE_IRQHandler
    weak_alias EXTI9_5_IRQHandler
    weak_alias TIM1_BRK_IRQHandler
    weak_alias TIM1_UP_IRQHandler
    weak_alias TIM1_TRG_COM_IRQHandler
    weak_alias TIM1_CC_IRQHandler
    weak_alias TIM2_IRQHandler
    weak_alias TIM3_IRQHandler
    weak_alias TIM4_IRQHandler
    weak_alias I2C1_EV_IRQHandler
    weak_alias I2C1_ER_IRQHandler
    weak_alias I2C2_EV_IRQHandler
    weak_alias I2C2_ER_IRQHandler
    weak_alias SPI1_IRQHandler
    weak_alias SPI2_IRQHandler
    weak_alias USART1_IRQHandler
    weak_alias USART2_IRQHandler
    weak_alias USART3_IRQHandler
    weak_alias EXTI15_10_IRQHandler
    weak_alias RTC_Alarm_IRQHandler
    weak_alias USBWakeUp_IRQHandler

    .end
