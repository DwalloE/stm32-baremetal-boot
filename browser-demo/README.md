# browser-demo - an honestly-labeled port, not the real thing

Browser Wokwi compiles STM32 projects only through the stm32duino Arduino
core (verified at project start: `board-stm32-bluepill` projects offer the
Arduino and PlatformIO-Arduino builders, no path for a raw `startup.s` +
linker script). So the saved browser project runs `sketch.ino`, in which
**stm32duino's own startup code has already run** before `setup()`. The
port therefore cannot demonstrate the two things this repo is actually
about - the hand-written vector table and reset handler - and does not
pretend to.

What it *does* do, with the same register addresses as `src/regs.h`:

- reads `RCC_CR` / `RCC_CFGR` / `FLASH_ACR` back and prints the clock tree
  the core actually configured (stm32duino runs the F103 at 72 MHz from
  HSE x 9, the same plan as `src/main.c`)
- prints the linker symbols `_sidata/_sdata/_edata/_sbss/_ebss/_estack` that
  stm32duino's own linker script exports, and the live MSP - the `map`
  command
- dumps the vector table the core is using through `SCB_VTOR` - the `vec`
  command - so you can see entry 0 = stack top and entry 1 = the core's
  `Reset_Handler`, Thumb bit set
- the `fault` command executes `UDF #0` and stm32duino's default
  HardFault handler halts the chip (no decoder there - that is `src/fault.c`)

The real firmware - `startup.s`, `stm32f103c8.ld`, the boot-integrity
control build - is what CI runs in Renode and in Wokwi via `wokwi-cli`,
and what the README's evidence comes from.
