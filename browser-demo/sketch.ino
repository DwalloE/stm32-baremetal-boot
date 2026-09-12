/*
 * stm32-baremetal-boot - browser demo (stm32duino port, clearly NOT the real firmware).
 *
 * Browser Wokwi can only compile STM32 through the Arduino core, whose own
 * startup.s and linker script have already run by the time setup() starts.
 * So this sketch cannot show the hand-written vector table or reset handler
 * from the repo - it shows the register-level READBACKS instead, with the
 * same addresses as src/regs.h, and the same shell commands.
 *
 * The real thing (startup.s, stm32f103c8.ld, the control build whose boot
 * check must fail) runs in CI: Renode + wokwi-cli. See ../README.md.
 *
 * Board: STM32 Blue Pill (board-stm32-bluepill), Serial = USART1 on PA9/PA10.
 */
#include <stdint.h>

#define REG32(a)   (*(volatile uint32_t *)(a))
#define RCC_CR     REG32(0x40021000u)   /* RM0008 Rev 15 §7.3.1 p.99 */
#define RCC_CFGR   REG32(0x40021004u)   /* §7.3.2 p.101 */
#define FLASH_ACR  REG32(0x40022000u)   /* §3.3.3 p.61 */
#define STK_LOAD   REG32(0xE000E014u)   /* PM0056 Rev 5 §4.5.2 p.152 */
#define SCB_VTOR   REG32(0xE000ED08u)   /* §4.4.4 p.133 */

/* stm32duino's linker script exports the same six symbols this repo's script does. */
extern "C" uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

static uint32_t read_msp() {
  uint32_t sp;
  __asm volatile("mrs %0, msp" : "=r"(sp));
  return sp;
}

static void hex(uint32_t v) {
  char b[11];
  snprintf(b, sizeof b, "0x%08lx", (unsigned long)v);
  Serial.print(b);
}

static void print_clk() {
  uint32_t cr = RCC_CR, cfgr = RCC_CFGR, acr = FLASH_ACR;
  uint32_t sws = (cfgr >> 2) & 3u;
  uint32_t pllmul = ((cfgr >> 18) & 0xFu) + 2u;
  bool pllsrc_hse = cfgr & (1u << 16);
  bool xtpre = cfgr & (1u << 17);
  uint32_t ppre1 = (cfgr >> 8) & 7u;

  Serial.print("clk: RCC_CR="); hex(cr);
  Serial.print(" RCC_CFGR="); hex(cfgr);
  Serial.print(" FLASH_ACR="); hex(acr);
  Serial.print(" STK_LOAD="); hex(STK_LOAD);
  Serial.println();
  Serial.print("clk: HSERDY="); Serial.print((cr >> 17) & 1u);
  Serial.print(" PLLRDY="); Serial.print((cr >> 25) & 1u);
  Serial.print(" SWS="); Serial.print(sws == 2 ? "PLL" : sws == 1 ? "HSE" : "HSI");
  if (sws == 2) {
    Serial.print(" (");
    Serial.print(pllsrc_hse ? (xtpre ? "HSE/2" : "HSE") : "HSI/2");
    Serial.print(" x"); Serial.print(pllmul);
    Serial.print(pllsrc_hse ? (xtpre ? " = 4 x " : " = 8 x ") : " = 4 x ");
    Serial.print(pllmul); Serial.print(" MHz");
    Serial.print(")");
  }
  Serial.print(", flash "); Serial.print(acr & 7u); Serial.print(" WS");
  Serial.print(", APB1 /"); Serial.print(ppre1 & 4u ? (1u << ((ppre1 & 3u) + 1u)) : 1u);
  Serial.println(" - configured by stm32duino's SystemClock_Config, read back here");
}

static void print_map() {
  Serial.print("map: _sidata="); hex((uint32_t)&_sidata);
  Serial.print(" _sdata="); hex((uint32_t)&_sdata);
  Serial.print(" _edata="); hex((uint32_t)&_edata);
  Serial.print(" ("); Serial.print((uint32_t)&_edata - (uint32_t)&_sdata); Serial.println(" bytes copied)");
  Serial.print("map: _sbss="); hex((uint32_t)&_sbss);
  Serial.print(" _ebss="); hex((uint32_t)&_ebss);
  Serial.print(" ("); Serial.print((uint32_t)&_ebss - (uint32_t)&_sbss); Serial.println(" bytes zeroed)");
  Serial.print("map: _estack="); hex((uint32_t)&_estack);
  Serial.print(" msp now="); hex(read_msp());
  Serial.print(" ("); Serial.print((uint32_t)&_estack - read_msp()); Serial.println(" bytes in use)");
  Serial.println("map: (stm32duino's linker script - the repo's own is src/stm32f103c8.ld)");
}

static void print_vec() {
  static const char *const names[16] = {
    "_estack", "Reset_Handler", "NMI_Handler", "HardFault_Handler",
    "MemManage_Handler", "BusFault_Handler", "UsageFault_Handler", "(reserved)",
    "(reserved)", "(reserved)", "(reserved)", "SVC_Handler",
    "DebugMon_Handler", "(reserved)", "PendSV_Handler", "SysTick_Handler" };
  uint32_t vtor = SCB_VTOR;
  const volatile uint32_t *tbl = (const volatile uint32_t *)(vtor ? vtor : 0x08000000u);
  Serial.print("vec: table at "); hex((uint32_t)tbl);
  Serial.print(" (VTOR="); hex(vtor); Serial.println(")");
  for (uint32_t i = 0; i < 16; i++) {
    Serial.print("vec["); Serial.print(i); Serial.print("] = "); hex(tbl[i]);
    Serial.print("  "); Serial.println(names[i]);
  }
  Serial.print(tbl[0] == (uint32_t)&_estack ? "vec: entry[0] == _estack, " : "vec MISMATCH: entry[0] != _estack, ");
  Serial.println((tbl[1] & 1u) ? "entry[1] has the Thumb bit" : "entry[1] Thumb bit CLEAR");
}

static void help() {
  Serial.println("shell: commands: help | clk | map | vec | fault   (stm32duino port - see browser-demo/README.md)");
}

static char line[32];
static uint32_t line_len;
static uint32_t next_uptime = 1000, seconds;

void setup() {
  pinMode(PC13, OUTPUT);
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("stm32-baremetal-boot: BROWSER DEMO - stm32duino port, the Arduino core's startup already ran");
  print_clk();
  print_vec();
  help();
}

void loop() {
  uint32_t now = millis();
  digitalWrite(PC13, (now / 500) & 1);
  if ((int32_t)(now - next_uptime) >= 0) {
    seconds++;
    Serial.print("uptime: t="); Serial.print(seconds); Serial.println("s");
    next_uptime += 1000;
  }
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      Serial.println();
      line[line_len] = 0;
      if (!strcmp(line, "help")) help();
      else if (!strcmp(line, "clk")) print_clk();
      else if (!strcmp(line, "map")) print_map();
      else if (!strcmp(line, "vec")) print_vec();
      else if (!strcmp(line, "fault")) {
        Serial.println("fault: executing UDF #0 - the Arduino core's HardFault handler halts here (no decoder in the port)");
        Serial.flush();
        __asm volatile("udf #0");
      } else if (line[0]) {
        Serial.print("shell: unknown command '"); Serial.print(line); Serial.println("'");
      }
      line_len = 0;
    } else if (line_len < sizeof line - 1) {
      line[line_len++] = c;
      Serial.print(c);
    }
  }
}
