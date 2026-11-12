# Builds every program into build/<Name>.bin. Run by GitHub Actions
# (see .github/workflows/build.yml), not on your own computer.
#
# To add a program: make a folder with a main.c in it and add its name here.
PROGRAMS = Blink HelloScreen Fish

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

HOSTCC  = gcc

CFLAGS  = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os \
          -Wall -Wextra -Werror -Wdouble-promotion \
          -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-math-errno \
          -ffunction-sections -fdata-sections -Icommon -Ibuild/gen
LDFLAGS = -nostdlib -T common/link.ld -Wl,--gc-sections -lgcc

# Code shared by every program: startup, the screen driver, the font.
COMMON  = $(wildcard common/*.c)

all: $(PROGRAMS:%=build/%.bin)

build/%.bin: build/%.elf
	$(OBJCOPY) -O binary $< $@
	$(SIZE) $<

build/%.elf: FORCE
	mkdir -p build
	$(CC) $(CFLAGS) $(wildcard $*/*.c) $(COMMON) $(LDFLAGS) -Wl,-Map=build/$*.map -o $@

# The fish brains normally come from the "Train fish brains" workflow, which the build
# downloads into build/gen. If there are none (or they don't fit the current rules), a
# short training run here makes some.
build/Fish.elf: build/gen/fish_brains.h build/gen/talk_model.h

# The talk model comes from the "Train fish talk" workflow (the build downloads it into
# build/gen). Until one has been trained, a stand-in keeps the animals quiet.
build/gen/talk_model.h:
	mkdir -p build/gen
	cp Fish/talk_model_none.h $@
build/gen/fish_brains.h:
	mkdir -p build/gen
	$(HOSTCC) -O2 -Wall -fno-math-errno -IFish tools/train_fish.c Fish/fish_sim.c -lm -o build/train_fish
	build/train_fish --brains $@ --results build/gen/fish_training.json

# The same fish rules and trained brains, built for web browsers (for the status website).
web: build/fish.wasm
build/fish.wasm: build/gen/fish_brains.h build/gen/talk_model.h FORCE
	clang --target=wasm32 -O2 -ffreestanding -fno-builtin -nostdlib -Wall \
	  -Wl,--no-entry -Wl,--export-dynamic -IFish -Ibuild/gen \
	  tools/fish_web.c Fish/fish_sim.c Fish/fish_view.c Fish/fish_talk.c -o $@

clean:
	rm -rf build

FORCE:
.PHONY: all web clean FORCE
.SECONDARY:
