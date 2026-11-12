// The board's screen: 240 dots wide, 320 dots tall, each dot any colour.
// (0, 0) is the top-left corner; x goes right, y goes down.
#ifndef LCD_H_INCLUDED
#define LCD_H_INCLUDED

#include <stdint.h>

#define LCD_W 240
#define LCD_H 320

// Mix a colour from red, green and blue, each 0 (none) to 255 (full).
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define BLACK  RGB(0, 0, 0)
#define WHITE  RGB(255, 255, 255)
#define RED    RGB(255, 0, 0)
#define GREEN  RGB(0, 200, 0)
#define BLUE   RGB(0, 0, 255)
#define YELLOW RGB(255, 220, 0)
#define NAVY   RGB(10, 20, 60)

void lcd_init(void);

// Fill a rectangle: left edge x, top edge y, w wide, h tall.
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);

// Write text with its top-left corner at (x, y). Each letter is 6*scale wide and
// 8*scale tall, including the gap. fg is the letter colour, bg the colour behind it.
void lcd_text(int x, int y, const char *text, int scale, uint16_t fg, uint16_t bg);

// --- Fast drawing, for moving pictures ---
// 1. lcd_area(...) picks a rectangle.  2. lcd_push(colour) once per dot, left to right,
// top to bottom.  3. lcd_done() when the rectangle is full.
void lcd_area(int x, int y, int w, int h);
void lcd_done(void);

#define LCD_SPI_SR (*(volatile uint32_t *)0x40015008)
#define LCD_SPI_DR (*(volatile uint32_t *)0x4001500C)

static inline void lcd_push(uint16_t color) {
  while (!(LCD_SPI_SR & 2)) {} // wait for room, then send the two halves of the colour
  LCD_SPI_DR = color >> 8;
  while (!(LCD_SPI_SR & 2)) {}
  LCD_SPI_DR = color & 0xFF;
}

#endif
