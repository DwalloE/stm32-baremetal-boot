/*
 * Linker script for the STM32F103C8 (Cortex-M3, "medium-density" F1).
 *
 * This is the file that decides where every byte lives. The startup code
 * (startup.s) consumes six symbols defined here - _sidata, _sdata, _edata,
 * _sbss, _ebss, _estack - and nothing else; the C code additionally reads
 * _sstack for the stack-pointer range check. Everything else is convention
 * that GNU ld and GCC agree on (section names, alignment).
 *
 * Memory map: RM0008 Rev 21 §2.3 "Memory map" and §3.3 "Embedded flash memory":
 *   flash 0x0800_0000, 64 KiB on the C8 part (128 KiB dies are common on
 *   "Blue Pill" boards, but the datasheet number is what we claim)
 *   SRAM  0x2000_0000, 20 KiB
 * After reset, flash is also aliased at 0x0000_0000 when BOOT0 = 0 (RM0008
 * §3.4 "Boot configuration"); the core fetches its initial MSP and PC from
 * address 0, which is why the vector table must be the first thing in flash.
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/*
 * The stack grows DOWN from the top of RAM. Cortex-M pushes a full-descending
 * stack (ARMv7-M ARM B1.4.1), so "top of RAM" is the first address the CPU
 * will never write. Vector table entry 0 is this value (startup.s).
 * _sstack is the lowest address the stack may legally reach; the boot check
 * verifies MSP is inside [_sstack, _estack] and the ASSERT below verifies
 * nothing else was linked into that window.
 */
_estack     = ORIGIN(RAM) + LENGTH(RAM);
_stack_size = 1K;
_sstack     = _estack - _stack_size;

SECTIONS
{
    /*
     * The vector table. KEEP() defeats --gc-sections: nothing references the
     * table by symbol, the HARDWARE reads it by address, so without KEEP the
     * linker would throw it away and the chip would boot into garbage.
     */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* Code and read-only data stay in flash and execute in place. */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        *(.glue_7)          /* ARM/Thumb interworking veneers (none on M3, kept for honesty) */
        *(.glue_7t)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Unwind tables: GCC emits these for C even with -fno-exceptions. Harmless. */
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH

    /*
     * Initialized data: lives in RAM at run time (VMA) but its initial image
     * has to come from somewhere non-volatile, so its LOAD address (LMA) is in
     * flash right after .text. "> RAM AT> FLASH" is that split. The startup
     * code copies LMA -> VMA, _sidata .. +(_edata-_sdata), before main.
     * This is why every initialized global costs flash AND RAM.
     */
    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    /*
     * Zero-initialized data: only an address range. Costs RAM, not flash.
     * NOLOAD is implicit for .bss; the startup code zeroes [_sbss, _ebss).
     * COMMON catches tentative definitions if anyone compiles without -fno-common.
     */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /*
     * No heap: this firmware never calls malloc (no libc is linked). _end is
     * defined anyway so a future newlib _sbrk has the conventional symbol.
     */
    . = ALIGN(8);
    _end = .;
    PROVIDE(end = .);

    /*
     * Reserve the stack window at the top of RAM as an explicit, NOLOAD
     * section so it shows up in the map with a size, and so the linker
     * itself refuses a build whose .data + .bss collide with it.
     */
    ._stack (NOLOAD) :
    {
        . = _sstack;
        . = . + _stack_size;
    } > RAM

    ASSERT(_ebss <= _sstack, "RAM overflow: .data + .bss run into the stack window")

    /* Debug sections stay in the ELF and never reach flash. */
    .ARM.attributes 0 : { *(.ARM.attributes) }
    /DISCARD/ : { *(.note.GNU-stack) }
}
