# Builds Blink.bin. Run by GitHub Actions (see .github/workflows/build.yml).
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

CFLAGS  = -mcpu=cortex-m4 -mthumb -Os -Wall -Wextra -Werror \
          -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns \
          -ffunction-sections -fdata-sections
LDFLAGS = -nostdlib -T link.ld -Wl,--gc-sections -Wl,-Map=Blink.map

Blink.bin: Blink.elf
	$(OBJCOPY) -O binary $< $@
	$(SIZE) $<

Blink.elf: main.c startup.c link.ld
	$(CC) $(CFLAGS) main.c startup.c $(LDFLAGS) -o $@

clean:
	rm -f Blink.elf Blink.bin Blink.map
