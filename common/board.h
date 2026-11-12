// Things every program on this board can use: the two lights and a way to wait.
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

// A register is a fixed memory address that controls part of the chip.
#define REG(addr) (*(volatile uint32_t *)(addr))

void delay_ms(uint32_t ms);

void leds_init(void);
void led_green(int on); // LD3, pin PG13
void led_red(int on);   // LD4, pin PG14

#endif
