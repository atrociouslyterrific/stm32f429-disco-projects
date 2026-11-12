// Driver for the board's screen (an ILI9341 display).
//
// The chip talks to the screen over SPI: a simple link that sends one bit at a time on a
// data wire, in step with a clock wire. A third wire (D/C) says whether the byte being
// sent is a command ("set the drawing area") or data ("here are the colours").
//
// Wiring on the STM32F429I-DISCO:
//   clock  PF7 (SPI5 SCK)      data   PF9 (SPI5 MOSI)
//   select PC2 (low = talking to the screen)
//   D/C    PD13 (low = command, high = data)
// The board's motion sensor shares the same SPI wires; its select pin (PC1) is held high
// so it stays quiet.

#include "lcd.h"
#include "board.h"
#include "font.h"

#define RCC_AHB1ENR REG(0x40023830)
#define RCC_APB2ENR REG(0x40023844)

#define GPIOC 0x40020800u
#define GPIOD 0x40020C00u
#define GPIOF 0x40021400u
#define MODER(port)   REG((port) + 0x00)
#define OSPEEDR(port) REG((port) + 0x08)
#define BSRR(port)    REG((port) + 0x18)
#define AFRL(port)    REG((port) + 0x20)
#define AFRH(port)    REG((port) + 0x24)

#define SPI5_CR1 REG(0x40015000)
#define SPI5_SR  REG(0x40015008)
#define SPI5_DR  REG(0x4001500C)

static void pin_high(uint32_t port, int pin) { BSRR(port) = 1u << pin; }
static void pin_low(uint32_t port, int pin)  { BSRR(port) = 1u << (pin + 16); }

// mode: 1 = output, 2 = "alternate function" (the pin is run by SPI, not by us)
static void pin_mode(uint32_t port, int pin, uint32_t mode) {
  MODER(port) = (MODER(port) & ~(3u << (pin * 2))) | (mode << (pin * 2));
  OSPEEDR(port) |= 3u << (pin * 2); // fast edges
}

static void spi_send(uint8_t byte) {
  while (!(SPI5_SR & 2)) {} // wait until there's room to send
  SPI5_DR = byte;
  while (!(SPI5_SR & 1)) {} // wait until it has gone out
  (void)SPI5_DR;
}

static void command(uint8_t c) {
  pin_low(GPIOD, 13);
  spi_send(c);
  pin_high(GPIOD, 13);
}

static void send_color(uint16_t color) {
  spi_send((uint8_t)(color >> 8));
  spi_send((uint8_t)color);
}

// The screen's setup recipe: each line is a command, how many data bytes follow, then
// the bytes. These are the manufacturer's recommended power and colour settings.
static const uint8_t SETUP[] = {
  0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
  0xCF, 3, 0x00, 0xC1, 0x30,
  0xE8, 3, 0x85, 0x00, 0x78,
  0xEA, 2, 0x00, 0x00,
  0xED, 4, 0x64, 0x03, 0x12, 0x81,
  0xF7, 1, 0x20,
  0xC0, 1, 0x23,             // power
  0xC1, 1, 0x10,
  0xC5, 2, 0x3E, 0x28,
  0xC7, 1, 0x86,
  0x36, 1, 0x48,             // which way up the picture is
  0x3A, 1, 0x55,             // 16 bits of colour per dot
  0xB1, 2, 0x00, 0x18,       // refresh rate
  0xB6, 3, 0x08, 0x82, 0x27,
  0xF6, 3, 0x01, 0x00, 0x00, // take the picture from SPI
  0xF2, 1, 0x00,
  0x26, 1, 0x01,
  0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
  0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
};

void lcd_init(void) {
  RCC_AHB1ENR |= (1u << 2) | (1u << 3) | (1u << 5); // switch on ports C, D and F
  RCC_APB2ENR |= 1u << 20;                          // and SPI5
  (void)RCC_APB2ENR;

  pin_high(GPIOC, 1);  pin_mode(GPIOC, 1, 1);  // motion sensor: not selected
  pin_high(GPIOC, 2);  pin_mode(GPIOC, 2, 1);  // screen: not selected yet
  pin_high(GPIOD, 13); pin_mode(GPIOD, 13, 1); // D/C

  // Hand pins PF7, PF8 and PF9 to SPI5 (alternate function number 5).
  AFRL(GPIOF) = (AFRL(GPIOF) & ~(0xFu << 28)) | (5u << 28);
  AFRH(GPIOF) = (AFRH(GPIOF) & ~0xFFu) | 0x55u;
  pin_mode(GPIOF, 7, 2);
  pin_mode(GPIOF, 8, 2);
  pin_mode(GPIOF, 9, 2);

  // SPI5: we are in charge of the clock, 8 bits at a time. Its speed is the chip's speed
  // divided down: 16 MHz / 2 = 8 MHz normally, 84 MHz / 4 = 21 MHz after board_fast_clock.
  uint32_t divide_by_4 = cpu_hz > 16000000u ? (1u << 3) : 0;
  SPI5_CR1 = (1u << 2) | (1u << 8) | (1u << 9) | divide_by_4;
  SPI5_CR1 |= 1u << 6;

  pin_low(GPIOC, 2); // select the screen, and keep it selected

  command(0x01); // restart the screen
  delay_ms(150);

  for (unsigned i = 0; i < sizeof SETUP;) {
    command(SETUP[i++]);
    for (uint8_t n = SETUP[i++]; n; n--) spi_send(SETUP[i++]);
  }

  command(0x11); // wake up
  delay_ms(150);
  command(0x29); // picture on
}

// Tell the screen which rectangle the next colours are for.
static void set_area(int x, int y, int w, int h) {
  int x1 = x + w - 1, y1 = y + h - 1;
  command(0x2A);
  spi_send((uint8_t)(x >> 8)); spi_send((uint8_t)x);
  spi_send((uint8_t)(x1 >> 8)); spi_send((uint8_t)x1);
  command(0x2B);
  spi_send((uint8_t)(y >> 8)); spi_send((uint8_t)y);
  spi_send((uint8_t)(y1 >> 8)); spi_send((uint8_t)y1);
  command(0x2C);
}

void lcd_area(int x, int y, int w, int h) { set_area(x, y, w, h); }

void lcd_done(void) {
  while (!(SPI5_SR & 2)) {}    // last byte handed over...
  while (SPI5_SR & (1u << 7)) {} // ...and fully sent
  (void)SPI5_DR;               // lcd_push ignores what comes back; clear that out
  (void)SPI5_SR;
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color) {
  // keep the rectangle on the screen
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;

  set_area(x, y, w, h);
  for (int n = w * h; n; n--) send_color(color);
}

void lcd_text(int x, int y, const char *text, int scale, uint16_t fg, uint16_t bg) {
  const int cell_w = (FONT_COLS + 1) * scale, cell_h = (FONT_ROWS + 1) * scale;
  for (; *text; text++, x += cell_w) {
    if (x < 0 || y < 0 || x + cell_w > LCD_W || y + cell_h > LCD_H) continue; // off the edge
    const char *dots = font_glyph(*text);
    set_area(x, y, cell_w, cell_h);
    // Send the letter one screen row at a time; each font dot becomes scale x scale dots.
    for (int row = 0; row <= FONT_ROWS; row++) {
      for (int repeat = 0; repeat < scale; repeat++) {
        for (int col = 0; col <= FONT_COLS; col++) {
          int lit = dots && row < FONT_ROWS && col < FONT_COLS && dots[row * FONT_COLS + col] == 'X';
          for (int k = 0; k < scale; k++) send_color(lit ? fg : bg);
        }
      }
    }
  }
}
