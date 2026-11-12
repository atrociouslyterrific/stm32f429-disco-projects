// Startup: the few things that must happen before main() can run.
// When the chip powers on it reads two numbers from the very start of its flash
// memory: where the stack begins, and the address of the first code to run.

#include <stdint.h>

extern uint32_t _stack_top, _data_load, _data_start, _data_end, _bss_start, _bss_end;
int main(void);

void Reset_Handler(void) {
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
