#include "board.h"

#define RCC_AHB1ENR REG(0x40023830)
#define GPIOG_MODER REG(0x40021800)
#define GPIOG_BSRR  REG(0x40021818)

// SysTick: a countdown timer built into the processor.
#define SYST_CSR REG(0xE000E010)
#define SYST_RVR REG(0xE000E014)
#define SYST_CVR REG(0xE000E018)

#define CPU_HZ 16000000u // after power-on the chip runs from its internal 16 MHz clock

void delay_ms(uint32_t ms) {
  SYST_RVR = CPU_HZ / 1000 - 1; // count down once per millisecond
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
