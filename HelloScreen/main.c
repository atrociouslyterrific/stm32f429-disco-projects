// Hello Screen: the first program that uses the board's display.
//
// It shows a greeting, three labelled colour bars, and a count of the seconds since it
// started. The green light also blinks, so you can tell the program is alive even if
// nothing appears on the screen.
//
// Things to try: change the words, the colours, or the sizes below, then push the change.
// GitHub rebuilds it, and the flash page puts the new version on the board.

#include "board.h"
#include "lcd.h"

// Turn a number into text, e.g. 42 -> "42    " (padded so old digits get wiped).
static void number_to_text(uint32_t n, char *out, int width) {
  char digits[10];
  int count = 0;
  do { digits[count++] = (char)('0' + n % 10); n /= 10; } while (n);
  int i = 0;
  while (count && i < width) out[i++] = digits[--count];
  while (i < width) out[i++] = ' ';
  out[i] = 0;
}

int main(void) {
  leds_init();
  led_red(1); // red while the screen is being set up
  lcd_init();

  lcd_fill_rect(0, 0, LCD_W, LCD_H, NAVY);
  lcd_text(12, 30, "HELLO!", 6, WHITE, NAVY);
  lcd_text(39, 100, "STM32F429", 3, YELLOW, NAVY);

  // If a bar's colour doesn't match its label, the colour setting in lcd.c needs flipping.
  lcd_fill_rect(0, 150, LCD_W, 32, RED);
  lcd_text(10, 158, "RED", 2, WHITE, RED);
  lcd_fill_rect(0, 186, LCD_W, 32, GREEN);
  lcd_text(10, 194, "GREEN", 2, WHITE, GREEN);
  lcd_fill_rect(0, 222, LCD_W, 32, BLUE);
  lcd_text(10, 230, "BLUE", 2, WHITE, BLUE);

  lcd_text(10, 280, "SECONDS:", 2, WHITE, NAVY);
  led_red(0);

  uint32_t seconds = 0;
  char text[8];
  for (;;) {
    number_to_text(seconds, text, 7);
    lcd_text(118, 280, text, 2, YELLOW, NAVY);
    led_green(1);
    delay_ms(500);
    led_green(0);
    delay_ms(500);
    seconds++;
  }
}
