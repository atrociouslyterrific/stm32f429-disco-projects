#include "board.h"

#define RCC_AHB1ENR REG(0x40023830)
#define GPIOG_MODER REG(0x40021800)
#define GPIOG_BSRR  REG(0x40021818)

// SysTick: a countdown timer built into the processor.
#define SYST_CSR REG(0xE000E010)
#define SYST_RVR REG(0xE000E014)
#define SYST_CVR REG(0xE000E018)

uint32_t cpu_hz = 16000000u; // after power-on the chip runs from its internal 16 MHz clock
uint32_t slow_bus_hz = 16000000u;

#define RCC_CR      REG(0x40023800)
#define RCC_PLLCFGR REG(0x40023804)
#define RCC_CFGR    REG(0x40023808)
#define FLASH_ACR   REG(0x40023C00)

// The chip has a clock multiplier (the "PLL"). This feeds it the internal 16 MHz clock
// and asks for 16 / 16 * 336 / 2 = 168 MHz, the chip's normal top speed.
void board_fast_clock(void) {
  // Reading program memory takes longer than one tick at this speed: allow 5 extra,
  // and switch on the memory's read-ahead and caches.
  FLASH_ACR = 5u | (1u << 8) | (1u << 9) | (1u << 10);
  RCC_PLLCFGR = 16u | (336u << 6) | (0u << 16) | (7u << 24);
  RCC_CR |= 1u << 24;                 // start the multiplier
  while (!(RCC_CR & (1u << 25))) {}   // wait until it is steady
  // Parts of the chip that can't keep up get a divided clock: the screen's SPI link is on
  // a half-speed bus (84 MHz), I2C on a quarter-speed one (42 MHz).
  RCC_CFGR = (RCC_CFGR & ~((7u << 10) | (7u << 13) | (0xFu << 4) | 3u)) | (5u << 10) | (4u << 13) | 2u;
  while ((RCC_CFGR & (3u << 2)) != (2u << 2)) {} // wait for the switch-over
  cpu_hz = 168000000u;
  slow_bus_hz = 42000000u;
}

void delay_ms(uint32_t ms) {
  SYST_RVR = cpu_hz / 1000 - 1; // count down once per millisecond
  SYST_CVR = 0;
  SYST_CSR = 5;                 // start, using the processor clock
  while (ms) {
    if (SYST_CSR & (1u << 16)) ms--; // set each time the count reaches zero
  }
  SYST_CSR = 0;
}

void leds_init(void) {
  RCC_AHB1ENR |= 1u << 6; // switch on port G
  (void)RCC_AHB1ENR;
  // pins 13 and 14 as outputs (mode 01)
  GPIOG_MODER = (GPIOG_MODER & ~((3u << 26) | (3u << 28))) | (1u << 26) | (1u << 28);
}

void led_green(int on) { GPIOG_BSRR = 1u << (on ? 13 : 13 + 16); }
void led_red(int on)   { GPIOG_BSRR = 1u << (on ? 14 : 14 + 16); }
