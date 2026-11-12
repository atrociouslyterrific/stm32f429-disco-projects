// Startup: the few things that must happen before main() can run.
// When the chip powers on it reads two numbers from the very start of its flash
// memory: where the stack begins, and the address of the first code to run.

#include <stdint.h>

extern uint32_t _stack_top, _data_load, _data_start, _data_end, _bss_start, _bss_end;
int main(void);

void Reset_Handler(void) {
  // Switch on the chip's maths unit (FPU) so programs can use decimal numbers quickly.
  *(volatile uint32_t *)0xE000ED88 |= 0xFu << 20;
  __asm__ volatile("dsb\nisb");

  // Copy starting values of variables from flash into RAM, and zero the rest.
  uint32_t *src = &_data_load;
  for (uint32_t *dst = &_data_start; dst < &_data_end;) *dst++ = *src++;
  for (uint32_t *dst = &_bss_start; dst < &_bss_end;) *dst++ = 0;
  main();
  for (;;) {}
}

// If anything goes wrong (a crash), stop here rather than running off into nowhere.
void Default_Handler(void) {
  for (;;) {}
}

__attribute__((section(".vectors"), used))
static void (*const vectors[16])(void) = {
  (void (*)(void))&_stack_top, // where the stack begins
  Reset_Handler,               // first code to run
  Default_Handler,             // NMI
  Default_Handler,             // HardFault
  Default_Handler,             // MemManage
  Default_Handler,             // BusFault
  Default_Handler,             // UsageFault
  0, 0, 0, 0,
  Default_Handler,             // SVCall
  Default_Handler,             // DebugMonitor
  0,
  Default_Handler,             // PendSV
  Default_Handler,             // SysTick
};

// The compiler sometimes asks for these two helpers by name (for example when copying a
// block of data). There is no standard library on the chip, so they are provided here.
__attribute__((used)) void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  while (n--) *d++ = *s++;
  return dst;
}

__attribute__((used)) void *memset(void *dst, int value, __SIZE_TYPE__ n) {
  unsigned char *d = dst;
  while (n--) *d++ = (unsigned char)value;
  return dst;
}
