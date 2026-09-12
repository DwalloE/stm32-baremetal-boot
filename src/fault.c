/*
 * fault.c - proving vector 3 works: a deliberate UDF, caught and decoded.
 *
 * ARMv7-M ARM §B1.5.6 / PM0056 Rev 5 §2.3.7 p.39: on exception entry the core
 * pushes r0, r1, r2, r3, r12, lr, ReturnAddress, xPSR (frame[0..7]) onto the
 * active stack ("the structure of eight data words is referred as stack frame"). With
 * UsageFault disabled (SHCSR.USGFAULTENA = 0, the reset state) an undefined
 * instruction escalates to HardFault with HFSR.FORCED set and the original
 * reason left in CFSR.UFSR - which is what the decoder prints.
 */
#include "fault.h"
#include "regs.h"
#include "fmt.h"
#include "gpio.h"

static void decode_cfsr(uint32_t cfsr)
{
    if (cfsr & CFSR_UNDEFINSTR)  out_str(" UNDEFINSTR");
    if (cfsr & CFSR_INVSTATE)    out_str(" INVSTATE");
    if (cfsr & CFSR_INVPC)       out_str(" INVPC");
    if (cfsr & CFSR_NOCP)        out_str(" NOCP");
    if (cfsr & CFSR_UNALIGNED)   out_str(" UNALIGNED");
    if (cfsr & CFSR_DIVBYZERO)   out_str(" DIVBYZERO");
    if (cfsr & CFSR_IBUSERR)     out_str(" IBUSERR");
    if (cfsr & CFSR_PRECISERR)   out_str(" PRECISERR");
    if (cfsr & CFSR_IMPRECISERR) out_str(" IMPRECISERR");
    if (cfsr & CFSR_BFARVALID) {
        out_str(" BFAR=");
        out_hex32(SCB_BFAR);
    }
}

void hardfault_c(uint32_t *frame)
{
    uint32_t cfsr = SCB_CFSR;
    uint32_t hfsr = SCB_HFSR;

    out_str("hardfault: caught, pc=");
    out_hex32(frame[6]);
    out_str(" lr=");
    out_hex32(frame[5]);
    out_str(" xpsr=");
    out_hex32(frame[7]);
    out_str(" cfsr=");
    out_hex32(cfsr);
    decode_cfsr(cfsr);
    out_str(" hfsr=");
    out_hex32(hfsr);
    if (hfsr & HFSR_FORCED)  out_str(" FORCED");
    if (hfsr & HFSR_VECTTBL) out_str(" VECTTBL");
    out_nl();
    out_str("hardfault: halted");
    out_nl();

    gpio_led_off();
    for (;;) {
        /* A fault handler that returns to the faulting PC would fault again forever; stopping here is the honest option. */
    }
}

void fault_trigger_udf(void)
{
    __asm volatile ("udf #0");   /* permanently undefined (ARMv7-M ARM §A7.7.191); Thumb encoding 0xDE00 */
}
