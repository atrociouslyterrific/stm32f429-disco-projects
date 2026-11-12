// Fish: a school of twelve fish, each steered by its own small neural-network brain.
//
// Every fish senses food, the other fish and the walls, and its own brain decides how to
// turn and how hard to swim (the rules are in fish_sim.c). The brains they start with
// were trained on GitHub. They keep evolving here: when a fish starves, the fish that
// has been doing best has a baby with a slightly changed copy of its brain.
//
// Press the blue button to drop food. Hungry fish fade towards grey.

#include "board.h"
#include "lcd.h"
#include "fish_sim.h"
#include "fish_brains.h"

#define TANK_ROWS 296 // screen rows used by the water; the rest is the status line
#define REACH 9       // a fish fits inside a square this far around its middle

#define RCC_AHB1ENR REG(0x40023830)
#define GPIOA_IDR   REG(0x40020010) // the blue button is pin PA0 (1 = pressed)

#define SYST_CSR REG(0xE000E010)
#define SYST_RVR REG(0xE000E014)
#define SYST_CVR REG(0xE000E018)
#define FRAME_TICKS (16000000u / 30) // 30 pictures a second

static Tank tank;
static uint16_t water[TANK_ROWS];       // the water colour of each row (lighter at the top)
static float last_x[N_FISH], last_y[N_FISH];
static uint8_t tint[N_FISH][3];         // each fish's own colour: red, green, blue
static uint16_t paint[N_FISH][3];       // this frame's body, eye and tail colours
static struct { int x, y, shown; } crumb[N_FOOD];
static uint32_t born;

static const uint8_t STARTING_TINTS[N_FISH][3] = {
  {255, 140, 0},   {255, 90, 90},   {255, 210, 60},  {120, 230, 120},
  {90, 200, 255},  {200, 140, 255}, {255, 150, 200}, {240, 240, 240},
  {160, 255, 220}, {255, 180, 120}, {180, 220, 90},  {130, 160, 255},
};

// Which part of a fish is at this spot? Measured from the fish's middle:
// u = how far towards its nose, v = how far to its right.
// 0 = water, 1 = body, 2 = eye, 3 = tail.
static inline int fish_part(float u, float v) {
  float bu = u - 1.0f;
  if (bu * bu * (1.0f / 25.0f) + v * v * (1.0f / 6.76f) <= 1.0f) { // an oval 10 long, 5.2 wide
    float eu = u - 3.6f, ev = v + 1.0f;
    return (eu * eu + ev * ev < 0.9f) ? 2 : 1;
  }
  if (u < -3.0f && u >= -8.5f) { // the tail: a triangle widening backwards
    float half = 0.6f + (-3.0f - u) * 0.6f;
    if (v < half && v > -half) return 3;
  }
  return 0;
}

// Put plain water back over a rectangle.
static void paint_water(int x0, int y0, int x1, int y1) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > LCD_W - 1) x1 = LCD_W - 1;
  if (y1 > TANK_ROWS - 1) y1 = TANK_ROWS - 1;
  if (x1 < x0 || y1 < y0) return;
  lcd_area(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) lcd_push(water[y]);
  lcd_done();
}

// Draw fish i where it is now, wiping where it was, in one go (so it doesn't flicker).
static void draw_fish(int i) {
  const Fish *f = &tank.fish[i];
  int cx = (int)f->x, cy = (int)f->y, ox = (int)last_x[i], oy = (int)last_y[i];

  // A fish that jumped (a newborn appearing elsewhere): wipe the old spot separately.
  if (cx - ox > 3 || ox - cx > 3 || cy - oy > 3 || oy - cy > 3) {
    paint_water(ox - REACH, oy - REACH, ox + REACH, oy + REACH);
    ox = cx;
    oy = cy;
  }

  int x0 = (cx < ox ? cx : ox) - REACH, x1 = (cx > ox ? cx : ox) + REACH;
  int y0 = (cy < oy ? cy : oy) - REACH, y1 = (cy > oy ? cy : oy) + REACH;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > LCD_W - 1) x1 = LCD_W - 1;
  if (y1 > TANK_ROWS - 1) y1 = TANK_ROWS - 1;

  // Other fish close enough to share this rectangle get drawn in it too.
  int near[3], n_near = 0;
  const float close = 2.0f * REACH + 3.0f;
  for (int j = 0; j < N_FISH && n_near < 3; j++) {
    if (j == i) continue;
    float gx = tank.fish[j].x - f->x, gy = tank.fish[j].y - f->y;
    if (gx < close && gx > -close && gy < close && gy > -close) near[n_near++] = j;
  }

  lcd_area(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
  for (int py = y0; py <= y1; py++) {
    float ry = ((float)py + 0.5f) - f->y;
    float rx = ((float)x0 + 0.5f) - f->x;
    float u = rx * f->dx + ry * f->dy;  // towards the nose
    float v = ry * f->dx - rx * f->dy;  // towards its right
    for (int px = x0; px <= x1; px++) {
      uint16_t color;
      int part = fish_part(u, v);
      if (part) {
        color = paint[i][part - 1];
      } else {
        color = water[py];
        for (int k = 0; k < n_near; k++) {
          const Fish *o = &tank.fish[near[k]];
          float qx = ((float)px + 0.5f) - o->x, qy = ((float)py + 0.5f) - o->y;
          int other = fish_part(qx * o->dx + qy * o->dy, qy * o->dx - qx * o->dy);
          if (other) { color = paint[near[k]][other - 1]; break; }
        }
      }
      lcd_push(color);
      u += f->dx; // one step right on the screen
      v -= f->dy;
    }
  }
  lcd_done();
  last_x[i] = f->x;
  last_y[i] = f->y;
}

// Work out a fish's colours for this frame: its own tint, fading towards grey when hungry.
static void mix_paint(int i) {
  float glow = 0.45f + 0.55f * tank.fish[i].energy;
  int r = (int)((float)tint[i][0] * glow), g = (int)((float)tint[i][1] * glow), b = (int)((float)tint[i][2] * glow);
  paint[i][0] = RGB(r, g, b);
  paint[i][1] = RGB(10, 10, 30);
  paint[i][2] = RGB(r * 2 / 3, g * 2 / 3, b * 2 / 3);
}

static void draw_food(void) {
  for (int k = 0; k < N_FOOD; k++) {
    const Food *food = &tank.food[k];
    int x = (int)food->x, y = (int)food->y;
    if (crumb[k].shown && (!food->there || crumb[k].x != x || crumb[k].y != y)) {
      paint_water(crumb[k].x - 1, crumb[k].y - 1, crumb[k].x + 1, crumb[k].y + 1);
      crumb[k].shown = 0;
    }
    if (food->there) {
      lcd_fill_rect(x - 1, y - 1, 3, 3, RGB(255, 230, 120));
      crumb[k].x = x;
      crumb[k].y = y;
      crumb[k].shown = 1;
    }
  }
}

static void show_number(int x, uint32_t n) {
  char digits[10], text[7];
  int count = 0, i = 0;
  do { digits[count++] = (char)('0' + n % 10); n /= 10; } while (n);
  while (count && i < 6) text[i++] = digits[--count];
  while (i < 6) text[i++] = ' ';
  text[i] = 0;
  lcd_text(x, 304, text, 1, YELLOW, BLACK);
}

// A fish has starved: the best-doing fish has a baby, which takes its place.
static void replace_fish(int gone) {
  int parent = gone == 0 ? 1 : 0;
  for (int j = 0; j < N_FISH; j++)
    if (j != gone && tank.fish[j].score > tank.fish[parent].score) parent = j;

  Fish *baby = &tank.fish[gone];
  fish_place(baby, &tank.rng);
  baby->energy = 0.6f;
  brain_child(&baby->brain, &tank.fish[parent].brain, &tank.rng, 0.15f);

  // The baby looks like its parent, with a slightly different shade.
  for (int c = 0; c < 3; c++) {
    int shade = (int)tint[parent][c] + (int)(rng_next(&tank.rng) % 61u) - 30;
    if (shade < 70) shade = 70;
    if (shade > 255) shade = 255;
    tint[gone][c] = (uint8_t)shade;
  }
  born++;
}

int main(void) {
  leds_init();
  led_red(1);
  lcd_init();
  RCC_AHB1ENR |= 1u << 0; // switch on port A, for the button

  // Water: lighter blue at the top, darker at the bottom.
  for (int y = 0; y < TANK_ROWS; y++) {
    int r = 8 - 6 * y / TANK_ROWS, g = 60 - 44 * y / TANK_ROWS, b = 110 - 62 * y / TANK_ROWS;
    water[y] = RGB(r, g, b);
  }
  paint_water(0, 0, LCD_W - 1, TANK_ROWS - 1);
  lcd_fill_rect(0, TANK_ROWS, LCD_W, LCD_H - TANK_ROWS, BLACK);
  lcd_text(4, 304, "FED:", 1, WHITE, BLACK);
  lcd_text(76, 304, "BORN:", 1, WHITE, BLACK);
  lcd_text(160, 304, "BUTTON: FEED", 1, WHITE, BLACK);

  tank_init(&tank, 0x1234ABCDu);
  for (int i = 0; i < N_FISH; i++) {
    for (int w = 0; w < N_WEIGHTS; w++) tank.fish[i].brain.w[w] = FISH_BRAINS[i][w];
    for (int c = 0; c < 3; c++) tint[i][c] = STARTING_TINTS[i][c];
    last_x[i] = tank.fish[i].x;
    last_y[i] = tank.fish[i].y;
  }
  led_red(0);

  // A free-running countdown, used to keep a steady 30 pictures a second.
  SYST_RVR = 0xFFFFFF;
  SYST_CVR = 0;
  SYST_CSR = 5;

  uint32_t shown_fed = ~0u, shown_born = ~0u, frame = 0;
  int button_was = 0;
  for (;;) {
    uint32_t frame_start = SYST_CVR;

    int button = (int)(GPIOA_IDR & 1u);
    if (button && !button_was) {
      tank.rng ^= SYST_CVR | 1u; // the moment you press makes the food's spot unpredictable
      tank_drop_food(&tank);
    }
    button_was = button;

    tank_step(&tank);

    for (int i = 0; i < N_FISH; i++) {
      tank.fish[i].score *= 0.9995f; // old successes slowly count for less
      if (tank.fish[i].energy <= 0.0f) replace_fish(i);
    }

    for (int i = 0; i < N_FISH; i++) mix_paint(i);
    for (int i = 0; i < N_FISH; i++) draw_fish(i);
    draw_food();

    if (tank.total_eaten != shown_fed) { shown_fed = tank.total_eaten; show_number(30, shown_fed); }
    if (born != shown_born) { shown_born = born; show_number(108, shown_born); }

    led_green((++frame / 15) & 1); // heartbeat

    while (((frame_start - SYST_CVR) & 0xFFFFFFu) < FRAME_TICKS) {}
  }
}
