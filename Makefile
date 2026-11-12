# Builds every program into build/<Name>.bin. Run by GitHub Actions
# (see .github/workflows/build.yml), not on your own computer.
#
# To add a program: make a folder with a main.c in it and add its name here.
PROGRAMS = Blink HelloScreen

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

CFLAGS  = -mcpu=cortex-m4 -mthumb -Os -Wall -Wextra -Werror \
          -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns \
          -ffunction-sections -fdata-sections -Icommon
LDFLAGS = -nostdlib -T common/link.ld -Wl,--gc-sections

# Code shared by every program: startup, the screen driver, the font.
COMMON  = $(wildcard common/*.c)

all: $(PROGRAMS:%=build/%.bin)

build/%.bin: build/%.elf
	$(OBJCOPY) -O binary $< $@
	$(SIZE) $<

build/%.elf: FORCE
	mkdir -p build
	$(CC) $(CFLAGS) $(wildcard $*/*.c) $(COMMON) $(LDFLAGS) -Wl,-Map=build/$*.map -o $@

clean:
	rm -rf build

FORCE:
.PHONY: all clean FORCE
.SECONDARY:
