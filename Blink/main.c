// Blink: the first program for the STM32F429I-DISCO board.
//
// The board has two small lights next to the screen:
//   green (LD3) on pin PG13, red (LD4) on pin PG14.
// This program turns them on in turn: green for half a second, then red.
//
// There is no library here on purpose. A microcontroller is controlled by writing
// numbers into fixed memory addresses ("registers"); each one below is from the
// chip's reference manual (RM0090).

#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

// Clock control: every part of the chip is switched off until its clock is enabled.
#define RCC_AHB1ENR REG(0x40023830)
#define GPIOG_CLOCK (1u << 6)

// Port G: the group of pins the two lights are wired to.
#define GPIOG_MODER REG(0x40021800) // what each pin is used for (2 bits per pin)
#define GPIOG_BSRR  REG(0x40021818) // write a 1 to switch a pin on (low half) or off (high half)

#define GREEN 13
#define RED   14

// SysTick: a countdown timer built into the processor. We use it to wait.
#define SYST_CSR REG(0xE000E010)
#define SYST_RVR REG(0xE000E014)
#define SYST_CVR REG(0xE000E018)

#define CPU_HZ 16000000u // after power-on the chip runs from its internal 16 MHz clock

static void delay_ms(uint32_t ms) {
  SYST_RVR = CPU_HZ / 1000 - 1; // count down once per millisecond
  SYST_CVR = 0;
  SYST_CSR = 5;                 // start, using the processor clock
  while (ms) {
    if (SYST_CSR & (1u << 16)) ms--; // this bit is set each time the count reaches zero
  }
  SYST_CSR = 0;
}

static void on(int pin)  { GPIOG_BSRR = 1u << pin; }
static void off(int pin) { GPIOG_BSRR = 1u << (pin + 16); }

int main(void) {
  RCC_AHB1ENR |= GPIOG_CLOCK;
  (void)RCC_AHB1ENR; // read it back: gives the clock a moment to start

  // Set both pins to "output" (mode 01).
  GPIOG_MODER = (GPIOG_MODER & ~((3u << (GREEN * 2)) | (3u << (RED * 2))))
              | (1u << (GREEN * 2)) | (1u << (RED * 2));

  for (;;) {
    on(GREEN); off(RED);
    delay_ms(500);
    off(GREEN); on(RED);
    delay_ms(500);
  }
}
