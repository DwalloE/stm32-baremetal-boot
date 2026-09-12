/*
 * boot_check.c - did startup.s do its job? Asked at run time, in the image itself.
 *
 * .data: the whole section is compared word-for-word against its flash image
 *        at _sidata. The canary array below guarantees .data is not empty and
 *        gives the failure line a recognisable pattern to print.
 * .bss:  a canary that must read zero. In both simulators RAM powers up zeroed
 *        so this cannot fail there - said plainly in the README. On silicon it can.
 * sp:    MSP must lie inside the window the linker script reserved.
 *
 * The CONTROL build (make control) skips the .data copy in startup.s, and CI
 * requires this file to print the violation there. Otherwise the pass line
 * would be a claim nobody tested.
 */
#include "boot_check.h"
#include "regs.h"
#include "fmt.h"

/* Linker-script symbols. Their ADDRESSES are the values; the contents are irrelevant. */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _sstack, _estack;
extern void Reset_Handler(void);

#define DATA_PATTERN 0xC0DE1234u
static volatile uint32_t data_canary[4] = { DATA_PATTERN, 0x11111111u, 0x22222222u, 0x33333333u };
static volatile uint32_t bss_canary;

static uint32_t read_msp(void)
{
    uint32_t sp;
    __asm volatile ("mrs %0, msp" : "=r" (sp));
    return sp;
}

int boot_check_print(void)
{
    const volatile uint32_t *ram   = &_sdata;
    const volatile uint32_t *flash = &_sidata;
    uint32_t words = ((uint32_t)&_edata - (uint32_t)&_sdata) / 4u;
    uint32_t differ = 0;
    uint32_t first_bad = 0, first_ram = 0, first_flash = 0;
    uint32_t sp = read_msp();
    int data_ok, bss_ok, sp_ok;
    uint32_t i;

    for (i = 0; i < words; i++) {
        if (ram[i] != flash[i]) {
            if (differ == 0u) {
                first_bad = (uint32_t)&ram[i];
                first_ram = ram[i];
                first_flash = flash[i];
            }
            differ++;
        }
    }
    data_ok = (differ == 0u) && (data_canary[0] == DATA_PATTERN);
    bss_ok  = (bss_canary == 0u);
    sp_ok   = (sp > (uint32_t)&_sstack) && (sp <= (uint32_t)&_estack);

    if (data_ok && bss_ok && sp_ok) {
        out_str("boot: data ok, bss ok, sp ok");
        out_nl();
        return 1;
    }

    out_str("boot INTEGRITY VIOLATION:");
    if (!data_ok) {
        out_str(" data ");
        out_dec(differ);
        out_str("/");
        out_dec(words);
        out_str(" words differ from flash, first at ");
        out_hex32(first_bad);
        out_str(" ram=");
        out_hex32(first_ram);
        out_str(" flash=");
        out_hex32(first_flash);
        out_str(";");
    }
    if (!bss_ok) {
        out_str(" bss canary=");
        out_hex32(bss_canary);
        out_str(" not zero;");
    }
    if (!sp_ok) {
        out_str(" msp=");
        out_hex32(sp);
        out_str(" outside stack window;");
    }
    out_nl();
    return 0;
}

/* The table the core is using: VTOR, or the flash alias when VTOR is still 0 after reset. */
static const volatile uint32_t *active_table(uint32_t *vtor_out)
{
    uint32_t vtor = SCB_VTOR;
    *vtor_out = vtor;
    if (vtor == 0u) {
        /* RM0008 §3.4 Table 9 p.61: with BOOT0 = 0 flash is aliased at 0, so 0 and 0x08000000 are the same bytes. */
        return (const volatile uint32_t *)0x08000000u;
    }
    return (const volatile uint32_t *)vtor;
}

int vec_check_print(void)
{
    uint32_t vtor;
    const volatile uint32_t *tbl = active_table(&vtor);
    uint32_t want_sp = (uint32_t)&_estack;
    uint32_t want_pc = (uint32_t)Reset_Handler;   /* C already carries the Thumb bit for a function address */

    if (tbl[0] == want_sp && tbl[1] == want_pc) {
        out_str("vec: table at ");
        out_hex32((uint32_t)tbl);
        out_str(" (VTOR=");
        out_hex32(vtor);
        out_str("), entry[0] = _estack ");
        out_hex32(tbl[0]);
        out_str(", entry[1] = Reset_Handler ");
        out_hex32(tbl[1]);
        out_nl();
        return 1;
    }

    out_str("vec MISMATCH: entry[0]=");
    out_hex32(tbl[0]);
    out_str(" want ");
    out_hex32(want_sp);
    out_str(", entry[1]=");
    out_hex32(tbl[1]);
    out_str(" want ");
    out_hex32(want_pc);
    out_nl();
    return 0;
}

void boot_print_map(void)
{
    out_str("map: _sidata="); out_hex32((uint32_t)&_sidata);
    out_str(" _sdata=");      out_hex32((uint32_t)&_sdata);
    out_str(" _edata=");      out_hex32((uint32_t)&_edata);
    out_str(" (");            out_dec((uint32_t)&_edata - (uint32_t)&_sdata);
    out_str(" bytes copied)");
    out_nl();
    out_str("map: _sbss=");   out_hex32((uint32_t)&_sbss);
    out_str(" _ebss=");       out_hex32((uint32_t)&_ebss);
    out_str(" (");            out_dec((uint32_t)&_ebss - (uint32_t)&_sbss);
    out_str(" bytes zeroed)");
    out_nl();
    out_str("map: _sstack="); out_hex32((uint32_t)&_sstack);
    out_str(" _estack=");     out_hex32((uint32_t)&_estack);
    out_str(" msp now=");     out_hex32(read_msp());
    out_str(" (");            out_dec((uint32_t)&_estack - read_msp());
    out_str(" bytes in use)");
    out_nl();
}

void boot_print_vectors(void)
{
    static const char *const names[16] = {
        "_estack", "Reset_Handler", "NMI_Handler", "HardFault_Handler",
        "MemManage_Handler", "BusFault_Handler", "UsageFault_Handler", "(reserved)",
        "(reserved)", "(reserved)", "(reserved)", "SVC_Handler",
        "DebugMon_Handler", "(reserved)", "PendSV_Handler", "SysTick_Handler"
    };
    uint32_t vtor;
    const volatile uint32_t *tbl = active_table(&vtor);
    uint32_t i;

    for (i = 0; i < 16u; i++) {
        out_str("vec[");
        out_dec(i);
        out_str("] = ");
        out_hex32(tbl[i]);
        out_str("  ");
        out_str(names[i]);
        out_nl();
    }
}
