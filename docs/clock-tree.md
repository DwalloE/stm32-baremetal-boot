# The clock tree, with the arithmetic

Everything below is what `src/clocktree.c` computes and `src/rcc.c` writes,
and what `test/test_clocktree.c` pins on the host (100% branch coverage,
gated in CI). Manual references are RM0008 Rev 15 (DocID13902) and, for the
core, PM0056 Rev 5 (DocID15491), with page numbers.

## The tree (RM0008 §7.2, Figure 8, p.93)

```text
 HSI 8 MHz RC ──┬──────────────────────────────────────┐
                └── /2 ── 4 MHz ──┐                    │  SW[1:0]
                                  ├─ PLL ×2..×16 ──────┼──────► SYSCLK ≤ 72 MHz
 HSE 4–16 MHz ──┬── PLLXTPRE /1 ──┤                    │           │
  (8 MHz on    └── PLLXTPRE /2 ──┘                    │        HPRE (/1)
   Blue Pill)   └──────────────────────────────────────┘           │
                                                                 HCLK ──► SysTick (CLKSOURCE=1), AHB
                                                                   ├── PPRE1 /2 ──► PCLK1 ≤ 36 MHz (APB1)
                                                                   └── PPRE2 /1 ──► PCLK2 ≤ 72 MHz (APB2: USART1, GPIO)
```

Two facts on this diagram decide the whole design:

1. **The HSI's /2 into the PLL is not optional.** 8 MHz / 2 = 4 MHz, and
   PLLMUL tops out at ×16 (RCC_CFGR[21:18] = 1110, §7.3.2 p.102), so the
   internal oscillator can reach **64 MHz, never 72**. The plan doc said
   "HSI → PLL → 72 MHz"; the manual says no. `clk_plan(CLK_SRC_HSI, 0, 72 MHz)`
   returns `CLK_ERR_HSI_PATH_LIMIT`, and the test requires it to.
2. **APB1 is capped at 36 MHz** ("The maximum allowed frequency of the APB1
   domain is 36 MHz", §7.2 p.93), so any SYSCLK above 36 MHz forces
   PPRE1 = 100 (/2). APB2 and AHB run at SYSCLK.

## The shipped configuration: 8 MHz crystal × 9 = 72 MHz

| Field | Value | Register | Why |
|---|---|---|---|
| HSEON, wait HSERDY | 1 | RCC_CR[16], [17] (§7.3.1 p.100) | the Blue Pill's 8 MHz crystal |
| PLLSRC | 1 (HSE) | RCC_CFGR[16] (p.102) | |
| PLLXTPRE | 0 (/1) | RCC_CFGR[17] | 72 / 8 = 9, an integer; no need to halve |
| PLLMUL | 0111 (×9) | RCC_CFGR[21:18] | field value = multiplier − 2 |
| PLLON, wait PLLRDY | 1 | RCC_CR[24], [25] | |
| HPRE | 0000 (/1) | RCC_CFGR[7:4] | HCLK = 72 MHz |
| PPRE1 | 100 (/2) | RCC_CFGR[10:8] | PCLK1 = 36 MHz, the cap |
| PPRE2 | 000 (/1) | RCC_CFGR[13:11] | PCLK2 = 72 MHz |
| LATENCY | 010 (2 WS) | FLASH_ACR[2:0] (§3.3.3 p.61) | 48 < 72 ≤ 72 MHz band |
| PRFTBE | 1 | FLASH_ACR[4] | prefetch buffer, reset default kept |
| SW, wait SWS | 10 (PLL) | RCC_CFGR[1:0], [3:2] | the switch, and the hardware's confirmation |

The written CFGR is therefore `0x001D0400 | SW`: PLLMUL 0111 << 18 = 0x001C0000,
PLLSRC = 0x00010000, PPRE1 100 << 8 = 0x00000400, then SW = 10.

### Order of operations, and why

`rcc.c` does: FLASH_ACR first → HSE on → CFGR (PLL off) → PLL on → SW.

- Wait states go up **before** the frequency does. At 72 MHz with 0 WS the
  flash returns stale data and the core executes garbage; the manual's band
  table (p.61) is a requirement, not a tuning knob.
- PLLSRC/PLLXTPRE/PLLMUL "can be written only when PLL is disabled"
  (§7.3.2 p.102), so CFGR is programmed while PLLON = 0.
- SW is written last, and SWS is *read back*. SW is a request; SWS is what
  happened. The firmware's `clk:` line prints only when SWS agrees.

### Every wait is bounded

`RCC_WAIT_POLLS = 100000`. On silicon HSERDY takes a few hundred µs and
PLLRDY a few µs, far inside the bound. The bound exists for the simulators:
Renode's F103 exposes RCC_CR as a fixed tag (HSERDY and PLLRDY always read
1) and leaves RCC_CFGR **unmapped**, so SWS reads 0 forever. A firmware that
spun on it would hang silently; this one prints

```text
clk UNVERIFIED: hse_ready=1 pll_ready=1 sws=0x00000000 (want 0x00000002) after 100000 polls - RCC readback does not confirm the requested tree
```

and the Renode Robot suite asserts exactly that line, because it is the truth
about that model. Wokwi models the RCC and the same code prints

```text
clk: sysclk 72000000 Hz via PLL (HSE 8 MHz x9), SWS confirms PLL, flash 2 WS, APB1 /2
```

## The 64 MHz alternate: `make SYSCLK=hsi64`

HSI/2 = 4 MHz × 16 = 64 MHz. PLLSRC = 0, PLLMUL = 1110, 2 WS (48 < 64 ≤ 72),
PPRE1 = /2 (PCLK1 = 32 MHz). Useful on a board with no crystal; not the
default because Renode's SysTick is hard-wired to 72 MHz in its platform
file, so only the 72 MHz image keeps real time there.

## Flash wait states (FLASH_ACR.LATENCY, RM0008 §3.3.3 p.61)

| SYSCLK | LATENCY | `clk_flash_latency()` boundary tests |
|---|---|---|
| 0 < f ≤ 24 MHz | 000 (0 WS) | 8 MHz → 0, 24 000 000 → 0 |
| 24 < f ≤ 48 MHz | 001 (1 WS) | 24 000 001 → 1, 48 000 000 → 1 |
| 48 < f ≤ 72 MHz | 010 (2 WS) | 48 000 001 → 2, 72 000 000 → 2 |

The bands are inclusive at the top; the tests sit one hertz either side of
each edge.

## USART1 baud rate (RM0008 §27.3.4 p.791, Equation 1)

```text
Tx/Rx baud = fPCLK / (16 × USARTDIV)        USART_BRR = DIV_Mantissa[15:4] : DIV_Fraction[3:0]
```

Since the register is USARTDIV in 1/16 units, `BRR = round(fPCLK / baud)`:

| PCLK2 | baud | fPCLK / baud | BRR | mantissa.fraction |
|---|---|---|---|---|
| 72 MHz | 115200 | 625.000 | **0x0271** | 39 + 1/16 = 39.0625 (error 0.01%) |
| 64 MHz | 115200 | 555.556 | 0x022C | 34 + 12/16 = 34.75 (error 0.08%) |
| 8 MHz | 9600 | 833.333 | 0x0341 | 52 + 1/16 (error 0.04%) |

`clk_usart_brr()` refuses baud = 0, USARTDIV < 1 (BRR < 16) and anything
that will not fit 16 bits (72 MHz at 300 baud = 240000). Readable serial
output in Wokwi at 115200 is this table being right: the USART model
derives its bit time from the same PCLK2 the RCC model computes, so a wrong
prescaler prints garbage.

## SysTick (PM0056 §4.5, pp.150–153)

CLKSOURCE = 1 clocks the 24-bit down-counter from HCLK. It fires when it
wraps from 0, so N cycles per tick needs `RELOAD = N − 1`
(§4.5.2 p.152): 72 000 000 / 1000 − 1 = **71 999** for 1 ms. The exception
lands on vector 15, which `startup.s` points at the weak `SysTick_Handler`
that `systick.c` overrides; the `uptime: t=Ns` lines are that path working.

## What the host tests pin (test/test_clocktree.c, 59 checks)

- 72 MHz from HSE ×9, no XTPRE, PPRE1 /2, 2 WS - the shipped plan, field by field
- 64 MHz from HSI is the ceiling; 72 from HSI is refused as `CLK_ERR_HSI_PATH_LIMIT`
- direct HSI (8 MHz) and direct HSE: PLL stays off, PPRE1 /1, 0 WS
- 8 MHz → 36 MHz needs XTPRE (×4.5 is not a multiplier; 4 × 9 is); 36 MHz exactly does **not** need APB1 /2
- rejections: 0 Hz, > 72 MHz, crystal outside 4–16 MHz, PLL output < 16 MHz, non-integer multipliers, ×18
- BRR: the three rows above plus the three rejections
- SysTick reload for 72 and 8 MHz
- every error string

What the gate found: the `PLLMUL >= 2` guard was unreachable - the PLL floor
(16 MHz) and the largest PLL input (16 MHz HSE, but target == HSE is served
without the PLL) make the multiplier ≥ 2 by construction - and was removed.
Dead guards are untestable, and untestable code is exactly what a 100%
branch gate exists to refuse.
