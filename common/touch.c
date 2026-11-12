// Driver for the touch chip (an STMPE811) that sits behind the screen's touch layer.
//
// The chip is reached over I2C: a two-wire link (clock PA8, data PC9) where every device
// has an address and we read and write its numbered "registers".
//
// Every wait in here gives up after a while, so a missing or stuck touch chip can never
// freeze the program: touch just reports "not pressed".

#include "touch.h"
#include "board.h"

#define RCC_AHB1ENR REG(0x40023830)
#define RCC_APB1ENR REG(0x40023840)

#define GPIOA 0x40020000u
#define GPIOC 0x40020800u
#define MODER(port)   REG((port) + 0x00)
#define OTYPER(port)  REG((port) + 0x04)
#define OSPEEDR(port) REG((port) + 0x08)
#define AFRH(port)    REG((port) + 0x24)

#define I2C3_CR1   REG(0x40005C00)
#define I2C3_CR2   REG(0x40005C04)
#define I2C3_DR    REG(0x40005C10)
#define I2C3_SR1   REG(0x40005C14)
#define I2C3_SR2   REG(0x40005C18)
#define I2C3_CCR   REG(0x40005C1C)
#define I2C3_TRISE REG(0x40005C20)

#define CR1_PE    (1u << 0)
#define CR1_START (1u << 8)
#define CR1_STOP  (1u << 9)
#define CR1_ACK   (1u << 10)
#define CR1_SWRST (1u << 15)
#define SR1_SB    (1u << 0)
#define SR1_ADDR  (1u << 1)
#define SR1_BTF   (1u << 2)
#define SR1_RXNE  (1u << 6)
#define SR1_TXE   (1u << 7)
#define SR1_AF    (1u << 10) // "nobody answered"

#define CHIP 0x82 // the touch chip's address

static int working;
static int failures;

// Wait for a flag in SR1. Returns 0 if it never comes (or the chip didn't answer).
static int wait_for(uint32_t flag) {
  for (uint32_t tries = 200000; tries; tries--) {
    uint32_t sr1 = I2C3_SR1;
    if (sr1 & flag) return 1;
    if (sr1 & SR1_AF) break;
  }
  return 0;
}

static void i2c_setup(void) {
  uint32_t bus_mhz = slow_bus_hz / 1000000u; // 16 or 42
  I2C3_CR1 = CR1_SWRST;
  I2C3_CR1 = 0;
  I2C3_CR2 = bus_mhz;
  I2C3_CCR = bus_mhz * 5;   // 100,000 bits a second
  I2C3_TRISE = bus_mhz + 1;
  I2C3_CR1 = CR1_PE;
}

// Give up on a conversation that went wrong and leave the link ready for the next one.
static int fail(void) {
  I2C3_CR1 |= CR1_STOP;
  I2C3_SR1 = 0; // clear the "nobody answered" flag
  i2c_setup();
  return 0;
}

// Start a conversation and say which register we mean.
static int begin(uint8_t reg) {
  // Let the previous conversation finish closing first.
  for (uint32_t tries = 200000; tries && (I2C3_CR1 & CR1_STOP); tries--) {}
  I2C3_CR1 |= CR1_START;
  if (!wait_for(SR1_SB)) return 0;
  I2C3_DR = CHIP; // address, "I'm going to write"
  if (!wait_for(SR1_ADDR)) return 0;
  (void)I2C3_SR2;
  I2C3_DR = reg;
  if (!wait_for(SR1_TXE)) return 0;
  return 1;
}

static int write_reg(uint8_t reg, uint8_t value) {
  if (!begin(reg)) return fail();
  I2C3_DR = value;
  if (!wait_for(SR1_BTF)) return fail();
  I2C3_CR1 |= CR1_STOP;
  return 1;
}

// Read `count` bytes starting at a register. count must be 1, or 3 or more.
static int read_reg(uint8_t reg, uint8_t *out, int count) {
  if (!begin(reg)) return fail();
  if (!wait_for(SR1_BTF)) return fail();

  I2C3_CR1 |= CR1_START; // start again, this time to read
  if (!wait_for(SR1_SB)) return fail();
  I2C3_DR = CHIP | 1;
  if (!wait_for(SR1_ADDR)) return fail();

  if (count == 1) {
    I2C3_CR1 &= ~CR1_ACK; // "this is the only byte I want"
    (void)I2C3_SR2;
    I2C3_CR1 |= CR1_STOP;
    if (!wait_for(SR1_RXNE)) return fail();
    out[0] = (uint8_t)I2C3_DR;
    return 1;
  }

  I2C3_CR1 |= CR1_ACK;
  (void)I2C3_SR2;
  int left = count;
  while (left > 3) {
    if (!wait_for(SR1_RXNE)) return fail();
    *out++ = (uint8_t)I2C3_DR;
    left--;
  }
  // The last three bytes need a particular dance so the chip stops at the right moment.
  if (!wait_for(SR1_BTF)) return fail();
  I2C3_CR1 &= ~CR1_ACK;
  *out++ = (uint8_t)I2C3_DR;
  if (!wait_for(SR1_BTF)) return fail();
  I2C3_CR1 |= CR1_STOP;
  *out++ = (uint8_t)I2C3_DR;
  *out++ = (uint8_t)I2C3_DR;
  return 1;
}

int touch_init(void) {
  working = 0;
  failures = 0;

  RCC_AHB1ENR |= (1u << 0) | (1u << 2); // ports A and C
  RCC_APB1ENR |= 1u << 23;              // I2C3
  (void)RCC_APB1ENR;

  // PA8 (clock) and PC9 (data): run by I2C3 (alternate function 4), "open drain" so
  // that either end can pull the wire low.
  OTYPER(GPIOA) |= 1u << 8;
  OTYPER(GPIOC) |= 1u << 9;
  AFRH(GPIOA) = (AFRH(GPIOA) & ~(0xFu << 0)) | (4u << 0);
  AFRH(GPIOC) = (AFRH(GPIOC) & ~(0xFu << 4)) | (4u << 4);
  OSPEEDR(GPIOA) |= 3u << 16;
  OSPEEDR(GPIOC) |= 3u << 18;
  MODER(GPIOA) = (MODER(GPIOA) & ~(3u << 16)) | (2u << 16);
  MODER(GPIOC) = (MODER(GPIOC) & ~(3u << 18)) | (2u << 18);

  i2c_setup();

  // Is the chip there? Registers 0 and 1 hold its model number, 0x0811.
  uint8_t id_high = 0, id_low = 0;
  if (!read_reg(0x00, &id_high, 1) || !read_reg(0x01, &id_low, 1)) return 0;
  if (id_high != 0x08 || id_low != 0x11) return 0;

  // The manufacturer's recipe for switching on touch sensing.
  int ok = write_reg(0x03, 0x02); // restart the chip
  delay_ms(10);
  ok = ok && write_reg(0x03, 0x00);
  delay_ms(2);
  ok = ok && write_reg(0x04, 0x0C)  // power the touch and measuring parts
          && write_reg(0x20, 0x49); // how it measures
  delay_ms(2);
  ok = ok && write_reg(0x21, 0x01)
          && write_reg(0x41, 0x9A)  // smoothing and settling times
          && write_reg(0x4A, 0x01)
          && write_reg(0x4B, 0x01)  // empty its list of readings
          && write_reg(0x4B, 0x00)
          && write_reg(0x56, 0x01)
          && write_reg(0x58, 0x01)
          && write_reg(0x40, 0x01)  // go
          && write_reg(0x0B, 0xFF);
  delay_ms(2);
  working = ok;
  return ok;
}

int touch_read(int *x, int *y) {
  if (!working) return 0;

  uint8_t state = 0, waiting = 0, data[4] = {0, 0, 0, 0};
  int ok = read_reg(0x40, &state, 1);
  int pressed = ok && (state & 0x80);
  if (pressed) {
    ok = read_reg(0x4C, &waiting, 1); // how many readings it has ready
    if (ok && !waiting) return 0;     // finger just landed; nothing measured yet
  }
  if (pressed && ok) {
    ok = read_reg(0xD7, data, 4);   // the newest reading: 12 bits of X, 12 bits of Y
    write_reg(0x4B, 0x01);          // empty its list so the next reading is fresh
    write_reg(0x4B, 0x00);
  }
  if (!ok) {
    if (++failures > 5) working = 0; // the chip has stopped answering: stop asking
    return 0;
  }
  failures = 0;
  if (!pressed) return 0;

  int raw_x = (data[0] << 4) | (data[1] >> 4);
  int raw_y = ((data[1] & 0x0F) << 8) | data[2];

  // Turn the chip's raw numbers into screen positions (the board maker's calibration).
  raw_x = raw_x <= 3000 ? 3870 - raw_x : 3800 - raw_x;
  int sx = raw_x / 15;
  int sy = (raw_y - 360) / 11;
  if (sx < 0) sx = 0;
  if (sx > 239) sx = 239;
  if (sy < 0) sy = 0;
  if (sy > 319) sy = 319;
  *x = sx;
  *y = sy;
  return 1;
}
