# stm32-baremetal-boot - STM32F103C8 from the reset vector, no HAL, no CMSIS.
#
#   make              build/firmware.elf|hex|bin|map|lst  (72 MHz, HSE x9)
#   make control      build/control.elf: startup.s WITHOUT the .data copy -
#                     the boot-integrity check must FAIL on this image
#   make size         arm-none-eabi-size of both images
#   make check        tools/check_vectors.py on the image + its self-test
#   make SYSCLK=hsi64 the 64 MHz HSI/2 x 16 variant (Renode's SysTick is
#                     fixed at 72 MHz, so only the default keeps real time there)
#   make clean
#
# Toolchain: any arm-none-eabi-gcc >= 10 (CI: Ubuntu's gcc-arm-none-eabi 13).
# Override with  make CROSS=/path/to/arm-none-eabi-
#
# No libc is linked (-nostdlib): the image is only what src/ says it is,
# which keeps the map file readable and the size claims honest. -lgcc is
# there for any helper GCC decides it needs (none on Cortex-M3 so far).

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size
NM      := $(CROSS)nm

SYSCLK  ?= hse72
BUILD   := build
LD      := src/stm32f103c8.ld

ARCH    := -mcpu=cortex-m3 -mthumb
CFLAGS  := $(ARCH) -std=c11 -Os -g3 -ffreestanding -fno-common \
           -ffunction-sections -fdata-sections \
           -Wall -Wextra -Werror -Wundef -Wshadow -Wconversion \
           -Isrc -DSYSCLK_$(SYSCLK)=1
ASFLAGS := $(ARCH) -g3
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -T $(LD) -Wl,--gc-sections \
           -Wl,--print-memory-usage
LDLIBS  := -lgcc

CSRC    := src/main.c src/boot_check.c src/rcc.c src/clocktree.c \
           src/gpio.c src/systick.c src/uart.c src/fmt.c src/fault.c
ASRC    := src/startup.s

OBJS         := $(patsubst src/%.c,$(BUILD)/%.o,$(CSRC)) $(BUILD)/startup.o
CONTROL_OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(CSRC)) $(BUILD)/startup_control.o

.PHONY: all control size lst check clean

all: $(BUILD)/firmware.elf $(BUILD)/firmware.hex $(BUILD)/firmware.bin $(BUILD)/firmware.lst

control: $(BUILD)/control.elf $(BUILD)/control.hex $(BUILD)/control.lst

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/startup.o: src/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

# The control image: the ONLY difference is one assembler symbol that
# removes the .data copy loop (see startup.s). Same C objects, same script.
$(BUILD)/startup_control.o: src/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -Wa,--defsym,CONTROL_SKIP_DATA_COPY=1 -c $< -o $@

$(BUILD)/firmware.elf: $(OBJS) $(LD)
	$(CC) $(LDFLAGS) -Wl,-Map=$(BUILD)/firmware.map -o $@ $(OBJS) $(LDLIBS)

$(BUILD)/control.elf: $(CONTROL_OBJS) $(LD)
	$(CC) $(LDFLAGS) -Wl,-Map=$(BUILD)/control.map -o $@ $(CONTROL_OBJS) $(LDLIBS)

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

# Disassembly with source interleaved; docs/boot-sequence.md quotes it.
$(BUILD)/%.lst: $(BUILD)/%.elf
	$(OBJDUMP) -d -S $< > $@

size: $(BUILD)/firmware.elf $(BUILD)/control.elf
	$(SIZE) $^ | tee $(BUILD)/size.txt

check: $(BUILD)/firmware.elf $(BUILD)/control.elf
	python3 tools/check_vectors.py --self-test
	python3 tools/check_vectors.py $(BUILD)/firmware.elf --cross=$(CROSS)
	python3 tools/check_vectors.py $(BUILD)/control.elf --cross=$(CROSS)

clean:
	rm -rf $(BUILD)

-include $(BUILD)/*.d
