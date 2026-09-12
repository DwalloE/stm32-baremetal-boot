# From power-on to `main()` - annotated, against this build's map

Every address below comes from this repo's CI: the `firmware` artifact of run
[34704457715](https://github.com/DwalloE/stm32-baremetal-boot/actions/runs/34704457715)
(`firmware.elf`, `firmware.map`, `firmware.lst`, `size.txt`; Ubuntu's
`gcc-arm-none-eabi` 13.2.1, `-Os`) and the Renode serial transcript of run
[34704629948](https://github.com/DwalloE/stm32-baremetal-boot/actions/runs/34704629948).
The local xPack 13.3.1 build is byte-for-byte the same size, so the numbers
are stable across the two compilers this project has met.

## The image

```text
$ arm-none-eabi-size build/firmware.elf build/control.elf
   text    data     bss     dec     hex filename
   5140      16    1068    6224    1850 build/firmware.elf
   5108      16    1068    6192    1830 build/control.elf
```

- `text` 5140 = `.isr_vector` (236) + `.text` incl. `.rodata` (4904). No libc:
  every byte is in `src/`.
- `data` 16 = the `.data` canary array in `boot_check.c`. It costs 16 bytes of
  flash (the image at `_sidata`) **and** 16 of RAM.
- `bss` 1068 = 44 bytes of zeroed globals + the 1024-byte `._stack` window the
  linker script reserves as a NOLOAD section. `size` bills NOLOAD RAM to `bss`;
  neither costs flash.
- control is 32 bytes smaller: that is the `.data` copy loop (`ldr/str` pair
  plus the three literal-pool addresses) that `CONTROL_SKIP_DATA_COPY` removes.

From `firmware.map`, the placement the startup code depends on:

```text
.isr_vector     0x08000000       0xec
.text           0x080000ec     0x1328
                0x08001414                        _sidata = LOADADDR (.data)
.data           0x20000000       0x10 load address 0x08001414
                0x20000000                        _sdata = .
                0x20000000       0x10 build/boot_check.o
                0x20000010                        _edata = .
.bss            0x20000010       0x2c load address 0x08001424
                0x20000010                        _sbss = .
 .bss.line_len  0x20000010        0x4 build/main.o
 .bss.line      0x20000014       0x20 build/main.o
                0x20000034        0x4 build/boot_check.o      (bss_canary)
 .bss.g_ms      0x20000038        0x4 build/systick.o
                0x2000003c                        _ebss = .
._stack         0x20004c00      0x400
```

Read that `.data` line twice: **VMA 0x20000000, LMA 0x08001414**. The section
lives in RAM at run time but its bytes are stored in flash right after `.text`.
Nothing moves them except the loop in `startup.s`. `_sidata` is `LOADADDR(.data)`
for exactly that reason.

## Step 0 - the core reads two words it did not execute

Reset (ARMv7-M ARM §B1.5.5; PM0056 Rev 5 §2.3.4 pp.35-36): the core reads
`[VTOR + 0]` into MSP and `[VTOR + 4]` into PC. VTOR resets to 0. On the F103,
address 0 aliases flash at 0x0800_0000 when BOOT0 = 0 (RM0008 Rev 15 §3.4
Table 9, p.61). So the first two words of the image decide everything, and
the linker script makes `.isr_vector` the first output section and `KEEP()`s
it against `--gc-sections`.

The bytes, from the artifact (`objdump -s -j .isr_vector`, little-endian), next
to the source that produced them:

```text
 8000000 00500020 250d0008 7f0d0008 6d0d0008      .word _estack            -> 0x20005000
 8000010 7f0d0008 7f0d0008 7f0d0008 00000000      .word Reset_Handler      -> 0x08000d25
 8000020 00000000 00000000 00000000 7f0d0008      .word NMI_Handler        -> 0x08000d7f (Default_Handler)
 8000030 7f0d0008 00000000 7f0d0008 cd0a0008      .word HardFault_Handler  -> 0x08000d6d
                                                    .word MemManage/BusFault/UsageFault -> 0x08000d7f x3
                                                    .word 0,0,0,0            reserved 7-10
                                                    .word SVC_Handler        -> 0x08000d7f
                                                    .word DebugMon_Handler   -> 0x08000d7f
                                                    .word 0                  reserved 13
                                                    .word PendSV_Handler     -> 0x08000d7f
                                                    .word SysTick_Handler    -> 0x08000acd
```

and the symbols they resolve to (`nm -n`):

```text
08000000 R g_vectors
08000acc T SysTick_Handler        entry[15] = 0x08000acd
08000d24 T Reset_Handler          entry[1]  = 0x08000d25
08000d6c T HardFault_Handler      entry[3]  = 0x08000d6d
08000d7e T Default_Handler        every weak alias = 0x08000d7f
20005000 R _estack                entry[0]  = 0x20005000
```

Three things to notice:

1. **Every handler word is odd.** The symbol is even (`0x08000d24`), the table
   holds `0x08000d25`. That low bit is EPSR.T, loaded on exception entry; a
   handler address with bit 0 clear is an INVSTATE UsageFault (ARMv7-M ARM
   §B1.5.6). The assembler set it because each handler is declared
   `.thumb_func` - that directive, not luck, is why the chip boots.
2. **Entry 0 is a stack pointer, not code.** `0x20005000` is the first byte
   *past* SRAM. The stack is full-descending, so the first push lands at
   `0x20004ffc`. The `map` command later reports `msp now=0x20004f98`: 104 bytes
   of stack in use at the shell prompt, out of the 1024 reserved.
3. **Eleven handlers share one address.** Everything not overridden in C is the
   weak alias to `Default_Handler`, a `b .` at `0x08000d7e`. SysTick (vector 15)
   is *not* among them - `systick.c` defines `SysTick_Handler` and the linker
   replaced the alias, which is the whole vector mechanism working with no
   registration call anywhere.

`tools/check_vectors.py` asserts all of this statically on every CI run (after
proving on six doctored tables that it can fail); the firmware's `vec:` line
asserts it at run time by reading the table back through VTOR.

## Step 1 - `Reset_Handler`: five jobs, thirty-two instructions

From `firmware.lst` (source interleaved), what the core executes first:

```text
08000d24 <Reset_Handler>:
    ldr     r0, =_estack
 8000d24:  480b        ldr   r0, [pc, #44]     @ (8000d54)   ; 0x20005000
    mov     sp, r0
 8000d26:  4685        mov   sp, r0
```

The hardware already did this. Re-doing it makes the entry point valid when
reached by a jump (a bootloader, a debugger's "run from reset vector") - one
instruction, marked `[CONV]` in the source because the architecture does not
ask for it.

```text
    ldr     r0, =_sdata              ; destination cursor    0x20000000
 8000d28:  480b        ldr   r0, [pc, #44]
    ldr     r1, =_edata              ; destination end       0x20000010
 8000d2a:  490c        ldr   r1, [pc, #48]
    ldr     r2, =_sidata             ; source cursor, flash  0x08001414
 8000d2c:  4a0c        ldr   r2, [pc, #48]
    b       .Ldata_check
 8000d2e:  e003        b.n   8000d38
.Ldata_copy:
 8000d30:  f852 3b04   ldr.w r3, [r2], #4
 8000d34:  f840 3b04   str.w r3, [r0], #4
.Ldata_check:
 8000d38:  4288        cmp   r0, r1
 8000d3a:  d3f9        bcc.n 8000d30
```

**This loop is the C standard's promise that initialized statics hold their
initializers at `main()`.** The architecture does nothing to keep it. Four
iterations here (16 bytes). The three addresses come from the linker script,
via the literal pool the assembler placed after the function.

In `control.elf` these fourteen bytes are absent (`grep -A12 Reset_Handler
control.lst` shows `ldr r0; mov sp, r0` followed directly by the `.bss` loop),
which is the entire difference between the two images.

```text
    ldr     r0, =_sbss               ; 0x20000010
    ldr     r1, =_ebss               ; 0x2000003c
    movs    r2, #0
    b       .Lbss_check
.Lbss_zero:
    str     r2, [r0], #4
.Lbss_check:
    cmp     r0, r1
    bcc     .Lbss_zero
```

Eleven words zeroed: `line_len`, `line[32]`, `bss_canary`, `g_ms`. Both
simulators power RAM up as zeros, so this loop is invisible there; silicon
does not, and `g_ms` starting at a random value would make the first
`uptime:` line lie.

```text
    bl      main
 8000d4e:  f7ff f9dd   bl    800010c <main>
.Lhang:
    b       .Lhang
```

`bl`, not `b`: if `main` ever returned, LR would point at the trap and a
debugger (or the HardFault decoder) would name the spot.

## Step 2 - what `main()` does before it prints anything

`src/main.c`, in order, and why the order:

1. `clk_plan(HSE, 8 MHz, 72 MHz)` - pure arithmetic → PLLMUL ×9, PPRE1 /2,
   2 wait states ([clock-tree.md](clock-tree.md)).
2. `rcc_apply()` - FLASH_ACR first, HSE on, CFGR while the PLL is off, PLL on,
   SW → PLL, every wait bounded at 100 000 polls.
3. `uart_init(clk_usart_brr(PCLK2, 115200))` - USART1 on PA9/PA10 with the
   divisor computed **from the bus clock the plan produced**, so a wrong
   prescaler prints garbage rather than a lie.
4. `gpio_led_init()`, `systick_init(71999)`.
5. Then the verdicts.

## Step 3 - the firmware grades the boot it just had

Renode, run 34704629948, healthy image (`usart1`, virtual time in brackets):

```text
[virt: 7ms] stm32-baremetal-boot: STM32F103C8, reset vector to main() with no HAL, no CMSIS
[virt: 7ms] boot: data ok, bss ok, sp ok
[virt: 7ms] vec: table at 0x08000000 (VTOR=0x08000000), entry[0] = _estack 0x20005000, entry[1] = Reset_Handler 0x08000d25
[virt: 7ms] clk UNVERIFIED: hse_ready=1 pll_ready=1 sws=0x00000000 (want 0x00000002) after 100000 polls - RCC readback does not confirm the requested tree
[virt: 7ms] shell: commands: help | boot | vec | clk | map | fault
[virt: 1.01s] uptime: t=1s
[virt: 2.01s] uptime: t=2s
```

- `boot: data ok` is a word-for-word comparison of `[_sdata, _edata)` against
  `[_sidata, ...)`, plus the `.bss` canary and MSP inside `[_sstack, _estack]`.
- `vec:` is the table read back through `SCB_VTOR` at run time and compared to
  `&_estack` and `Reset_Handler` as C sees them.
- `clk UNVERIFIED` is **correct for Renode**: its F103 platform exposes RCC_CR
  only as a fixed tag (`0x0A020083`, ready bits stuck high) and leaves RCC_CFGR
  unmapped, so SWS reads 0 and the firmware says so instead of spinning. The
  Robot suite asserts that line; the same code prints the PLL-confirmed line
  in Wokwi, whose RCC is modeled.
- `uptime: t=1s` at 1.01 s virtual is SysTick → vector 15 → `SysTick_Handler`
  → `g_ms`, with RELOAD = 71 999 against Renode's 72 MHz SysTick.

The control image, same run:

```text
[virt: 7ms] boot INTEGRITY VIOLATION: data 4/4 words differ from flash, first at 0x20000000 ram=0x00000000 flash=0xc0de1234;
[virt: 7ms] vec: table at 0x08000000 (VTOR=0x08000000), entry[0] = _estack 0x20005000, entry[1] = Reset_Handler 0x08000d25
[virt: 1.01s] uptime: t=1s
```

RAM held zeros where flash holds `0xC0DE1234`: the copy did not happen, the
check said so, and everything after it still ran - the verdict printer never
depended on `.data`. Without this run the healthy `boot: data ok` would be a
claim nobody had tested.

## Step 4 - proving vector 3 (the fault demo)

```text
[virt: 1.01s] fault: executing UDF #0 - HardFault_Handler must catch it
[virt: 1.01s] hardfault: caught, pc=0x08000d20 lr=0x08000277 xpsr=0x81000000 cfsr=0x00010000 UNDEFINSTR hfsr=0x40000000 FORCED
[virt: 1.01s] hardfault: halted
```

`pc=0x08000d20` is the `udf #0` inside `fault_trigger_udf`; `lr=0x08000277`
is its return address in `main`'s dispatcher. CFSR bit 16 (UNDEFINSTR) names
the cause, HFSR bit 30 (FORCED) says it escalated because UsageFault was never
enabled - the reset state. The `tst lr, #4 / mrseq / mrsne` shim in `startup.s`
chose MSP (EXC_RETURN bit 2 clear: thread mode was on the main stack) and
handed the eight-word frame (PM0056 §2.3.7 p.39) to C.

## The ten biggest symbols

`nm --size-sort` on the artifact - all of it is ours, there is no platform:

| Bytes | Symbol | Why |
|---:|---|---|
| 488 | `main` | the shell dispatcher and its strings' callers |
| 348 | `boot_check_print` | the word-by-word `.data` compare and the failure printer |
| 324 | `rcc_print_verdict` | two verdict formats |
| 300 | `hardfault_c` | CFSR/HFSR decode |
| 264 | `boot_print_map` | six symbols, three lines |
| 260 | `rcc_apply` | the sequence and its bounded waits |
| 260 | `clk_plan` | the arithmetic (host-tested) |
| 236 | `g_vectors` | 59 words |
| 184 | `vec_check_print` | |
| 104 | `boot_print_vectors` | |

The printing is bigger than the booting. That is the honest shape of a
firmware whose job is to explain itself over a UART.

## Honest limits of this document

Renode's timings are virtual-time numbers from an instruction-counting
model (about 3.5 s of host time per simulated second on the CI runner); they
say the SysTick arithmetic is right, not what silicon would do. Renode has no
BOOT0 pin and no flash alias at 0, so `bluepill.resc` sets VTOR explicitly
and the `vec:` line reads `VTOR=0x08000000`; on the chip it would read
`VTOR=0x00000000` and the same bytes through the alias. No oscillator start-up
time, PLL lock time, brown-out or flash wait-state *effect* is modeled anywhere
in this repo - the wait states are set because the manual says so, and no
simulator would notice if they were not.
