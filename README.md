# stm32-baremetal-boot

An STM32F103C8 (Cortex-M3, the "Blue Pill") brought up from the reset vector
with my own `startup.s`, vector table and linker script, and RCC / GPIO /
SysTick / USART programmed by register from the reference manual - no HAL, no
CMSIS, no libc - where **the firmware grades its own boot** and a control build
proves the grade can fail.

![demo](docs/demo.gif)

## Run it in your browser

**Wokwi project** - *link lands at close-out* - runs
[`browser-demo/sketch.ino`](browser-demo/sketch.ino), an honestly-labeled
stm32duino port: browser Wokwi compiles STM32 only through the Arduino core,
whose startup has already run before `setup()`, so the port shows the same
register readbacks (`clk`, `map`, `vec`, `fault`) but cannot show the
hand-written reset path. That is what CI simulates.

Locally (any `arm-none-eabi-gcc` ≥ 10; no libc needed):

```sh
make                 # build/firmware.elf|hex|map|lst  - 72 MHz from the 8 MHz crystal
make control         # build/control.elf - startup.s WITHOUT the .data copy
make size check      # arm-none-eabi-size + the static vector-table check (self-test first)
make -C test         # host: clock-tree arithmetic, 100% branch gate (~1 s, needs cc)
renode renode/bluepill.resc                       # interactive, USART1 analyzer opens
renode-test renode/boot.robot                     # the three Robot cases CI runs
wokwi-cli --scenario wokwi-ci.scenario.yaml .     # needs a free token from wokwi.com/dashboard/ci
```

## What this demonstrates

- **The vector table and reset handler, by hand** (`src/startup.s`): entry 0 is
  the initial MSP, entry 1 the Thumb-bit-set `Reset_Handler`, RM0008 Table 63's
  43 external positions, weak `Default_Handler` aliases that C overrides at
  link time. Every line is marked `[ARCH]` (the architecture requires it) or
  `[CONV]` (GCC and the linker script agree on it).
- **`.data` copy and `.bss` zeroing as the C standard's promise, kept by two
  loops** - and a linker script (`src/stm32f103c8.ld`) whose `> RAM AT> FLASH`
  is the reason `.data` has two addresses (`VMA 0x20000000, LMA 0x08001414` in
  this build's map). `_sidata/_sdata/_edata/_sbss/_ebss/_estack` are the whole
  contract between the two files; the stack is an explicit NOLOAD window the
  linker refuses to let `.bss` grow into.
- **The clock tree with the arithmetic shown** ([docs/clock-tree.md](docs/clock-tree.md)):
  8 MHz HSE × 9 = 72 MHz, APB1 /2 because it is capped at 36 MHz, 2 flash wait
  states because 48 < 72 ≤ 72, USART_BRR = 0x271 because 72 MHz / 115200 =
  625.0 - as a pure function with 100% branch coverage. The manual, not the
  brief, decides: HSI/2 × 16 caps at **64 MHz**, so 72 needs the crystal, and
  `clk_plan()` refuses the other request.
- **Memory-mapped registers with the page next to them** (`src/regs.h`): every
  address and bit cites RM0008 Rev 15 / PM0056 Rev 5 by section and page.
- **Fault handling that names the cause**: a deliberate `UDF` escalates to
  HardFault; an assembly shim picks MSP or PSP from EXC_RETURN and C decodes
  the stacked PC and CFSR (`UNDEFINSTR`, `FORCED`).

### The image, and the table matching its source

```text
   text    data     bss     dec     hex filename
   5140      16    1068    6224    1850 build/firmware.elf
   5108      16    1068    6192    1830 build/control.elf      (-32: the .data copy loop)
```

```text
$ arm-none-eabi-objdump -s -j .isr_vector build/firmware.elf        startup.s
 8000000 00500020 250d0008 7f0d0008 6d0d0008         .word _estack          = 0x20005000  top of SRAM
 8000010 7f0d0008 7f0d0008 7f0d0008 00000000         .word Reset_Handler    = 0x08000d25  (nm: 08000d24, +Thumb bit)
 8000020 00000000 00000000 00000000 7f0d0008         .word NMI_Handler      = 0x08000d7f  (Default_Handler)
 8000030 7f0d0008 00000000 7f0d0008 cd0a0008         .word HardFault_Handler= 0x08000d6d
                                                     ... reserved 7-10 = 0, SVC/DebugMon/PendSV = Default_Handler,
                                                     .word SysTick_Handler  = 0x08000acd  (overridden in systick.c)
```

[docs/boot-sequence.md](docs/boot-sequence.md) walks power-on → `main()`
against this map and disassembly, with the Renode transcripts.

## The bug gallery

What the tools caught while this was built - evidence, not war stories:

- **The first link overflowed RAM by 512 MiB.** Inside an output section the
  location counter is *section-relative*; `._stack : { . = _sstack; ... }`
  asked for 0x20004C00 bytes of padding. `ld` refused. The fix is to give the
  section its address (`._stack _sstack (NOLOAD) :`) - and the map now shows
  the window as a 0x400-byte section at 0x20004c00, which the `map` command
  and the boot check's MSP test both read.
- **The branch gate refused a dead guard.** `clk_plan()` checked
  `PLLMUL >= 2`, but the PLL floor is 16 MHz, the largest PLL input is 16 MHz,
  and a target equal to the crystal is served without the PLL - so the
  multiplier is ≥ 2 by construction and the branch could never fire. gcov
  would not give 100% until it was gone. Dead guards are untestable, and
  untestable code is what the gate exists to find (02's and 05's lesson, third
  time).
- **The brief's clock tree was wrong, and the arithmetic said so.** The plan
  doc specified "HSI → PLL → 72 MHz". RM0008 Figure 8 feeds the PLL from HSI
  through a fixed /2, and PLLMUL stops at ×16: 64 MHz is the ceiling.
  `test_64mhz_from_hsi_is_the_ceiling` pins both the ceiling and the refusal;
  the shipped image uses the crystal.
- **Robot Framework splits on two spaces.** The first CI run's Renode job
  failed with `Nullable`1 does not have a "Parse" method`: the two-space
  column gap inside a `vec[1] = ...  Reset_Handler` regex became a second
  positional argument, parsed as the `float? timeout`. `\s+` fixed it. The
  firmware itself had already printed every line the suite wanted (04's
  multi-space lesson, now in Robot form).
- **Wokwi's F103 never delivered a typed byte.** The first token-backed run
  ([34705510210](https://github.com/DwalloE/stm32-baremetal-boot/actions/runs/34705510210))
  passed every boot verdict - including the PLL-confirmed clock line - then
  timed out waiting for `map` to answer. A probe build counted PA10-low
  samples and RXNE events per second: RXNE stayed 0, and the PA10 count was
  *identical* (307, 336, 364 ...) across five USART configurations
  (baseline, BRR for 8 and 36 MHz, RX pull-up, RXNEIE) and unchanged by
  the write - deterministic floating-input noise, not a monitor driving the
  pin. Swapped or absent wiring killed TX too, so the wiring is right and
  the monitor's TX path simply does not reach the USART model (runs
  [34706011109](https://github.com/DwalloE/stm32-baremetal-boot/actions/runs/34706011109),
  [34706221452](https://github.com/DwalloE/stm32-baremetal-boot/actions/runs/34706221452)).
  Two more facts fell out: the monitor showed clean text even with the BRR
  computed for the wrong bus clock, so it reads bytes at the peripheral, not
  the pin - readable output is **not** evidence of the BRR arithmetic in
  Wokwi (the README no longer claims it is); and `--serial-log-file` drops
  bytes at chunk boundaries while the console stream is intact, so evidence
  is quoted from the console. Wokwi now asserts boot output only.
- **Renode has no RCC on the F103.** Verified before planning, not after:
  RCC_CR is a fixed tag (ready bits stuck high) and RCC_CFGR is unmapped. A
  firmware that spins on `SWS == PLL` hangs silently there. Every wait in
  `rcc.c` is bounded and the verdict prints what was read back -
  `clk UNVERIFIED: ... sws=0x00000000 (want 0x00000002) after 100000 polls` -
  and the Robot suite asserts that exact line as the truth about that model.

## How it is tested

![ci](https://github.com/DwalloE/stm32-baremetal-boot/actions/workflows/ci.yml/badge.svg)

- **Host** (`make -C test`): 59 checks on the clock arithmetic - the shipped
  plan field by field, the 64 MHz HSI ceiling, latency bands one hertz either
  side of each edge, XTPRE for 8 → 36 MHz, every rejection, BRR rounding,
  SysTick reload - and every branch of `clocktree.c` taken both ways (gated).
- **Static** (`tools/check_vectors.py`): `.isr_vector` at 0x08000000,
  entry 0 == `_estack` == top of SRAM, entry 1 == `Reset_Handler|1` == ELF
  entry, entries 2-15 by name, reserved slots zero, every handler odd. Its
  self-test runs first and must reject six doctored tables.
- **Renode** (`renode/boot.robot`, no token, no minute cap): the healthy image
  must print `boot: data ok, bss ok, sp ok`, the `vec:` line with the linked
  addresses, the honest `clk UNVERIFIED` line, `uptime: t=3s` from SysTick,
  and blink PC13 at 500/500 ms (`Assert LED Is Blinking`); `map` and `vec`
  must answer. **The control** (`build/control.elf`, `startup.s` without the
  `.data` copy) must print `boot INTEGRITY VIOLATION: data 4/4 words differ
  from flash ... ram=0x00000000 flash=0xc0de1234` - and `boot: data ok` is a
  registered failing string. **The fault case** types `fault` and requires
  `hardfault: caught ... UNDEFINSTR ... FORCED`.
- **Wokwi** (`wokwi-cli`, token-gated; Wokwi models the RCC): the healthy
  scenario requires `clk: sysclk 72000000 Hz via PLL (HSE 8 MHz x9), SWS
  confirms PLL, flash 2 WS, APB1 /2` plus the boot/vec lines and `uptime:
  t=5s`; the control run passes `--elf build/control.elf` and requires the
  violation with `--fail-text 'boot: data ok'`. No typed commands here: the
  monitor's input never reaches USART1 RX in Wokwi's F103 (see the gallery),
  so the shell is Renode's to test.

Pass and fail texts share no substring (`boot:` vs `boot INTEGRITY VIOLATION`,
`vec:` vs `vec MISMATCH`, `clk:` vs `clk UNVERIFIED`), so a scenario matching
one cannot be satisfied by the other.

## Honest limits

- **Two simulators, two partial models.** Renode's F103 has no RCC, no FLASH
  interface, no BOOT0 alias at 0 (`bluepill.resc` sets VTOR explicitly) and a
  SysTick fixed at 72 MHz whatever the firmware asked for; Wokwi's models the
  RCC and SysTick, lists DMA, IWDG, PWR and RTC as not implemented, and did
  not deliver serial-monitor input to USART1 RX from `wokwi-cli`. Neither
  models oscillator start-up, PLL lock time, or the *effect* of a wrong flash
  wait-state count - the 2 WS are set because RM0008 p.61 says so, and no
  simulator here would fail if they were not.
- **RAM is zero at power-up in both simulators**, so the `.bss` canary cannot
  fail there and the control's `ram=0x00000000` is the simulator's zero, not
  silicon's noise. On the chip the same check would print whatever the SRAM
  woke up holding.
- **Timing is simulated-scheduler timing.** `uptime: t=1s` at 1.01 s virtual
  says the RELOAD arithmetic is right; it says nothing about real crystal
  accuracy or the ~3.5× slower host time Renode needed per simulated second.
- **No electrical behaviour**: no brown-out, no reset-pin glitching, no
  power-on reset delay, no GPIO drive strength - the `2 MHz` output mode on
  PC13 is a register value, not a measured slew.
- **The browser demo is a port**, not the firmware; see
  [browser-demo/README.md](browser-demo/README.md).
- **64 KiB is the datasheet number** for the C8; many Blue Pill boards carry
  128 KiB dies. The linker script claims what the datasheet does.
