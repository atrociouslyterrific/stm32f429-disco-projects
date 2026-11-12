// Fish: ten fish, each one different, each steered by its own small neural-network brain.
//
// Every fish has its own size, shape, colour, markings and swimming pace, and its own
// brain that senses food, other fish, friends, enemies, prey, threats, walls, its hunger
// and its health, then decides how to turn, how hard to swim and whether to bite.
// (The rules of the tank are in fish_sim.c.)
//
//   Nametags       Every fish carries its name above it.
//   Feeding        Tap the water to drop food there. The blue button drops some at random.
//   Growing        The more a fish eats, the bigger it gets.
//   Hunger/health  The panel under the tank has a box per fish: green bar = health,
//                  orange bar = hunger. A starving fish loses health; a fed one heals.
//   Relationships  Fish that swim together become friends; fish that crowd each other,
//                  steal food or bite become enemies. Each box shows the colour of that
//                  fish's best friend (+) and worst enemy (-).
//   Eating         A hungry fish can bite a clearly smaller one that isn't its friend.
//                  The bitten fish flashes red. Enough bites and it is eaten.
//   Babies         When a fish dies, the fish doing best has a baby: similar looks and a
//                  slightly changed brain. So the tank's brains keep evolving.
//   Log            The three lines under the panel report what happens, newest at the bottom.

#include "board.h"
#include "lcd.h"
#include "font.h"
#include "touch.h"
#include "fish_sim.h"
#include "fish_brains.h"

// Screen layout, top to bottom
#define TANK_ROWS 224 // the water
#define CELL_W 48     // then two rows of five boxes, one per fish
#define CELL_H 32
#define BAR_W 42
#define LOG_Y 288     // then three lines of log
#define LOG_LINES 3
#define LOG_CHARS 40
#define COUNT_Y 312   // then the counters

#define TAG_W 18 // a nametag is three letters, 6 dots each
#define TAG_H 7

#define RCC_AHB1ENR REG(0x40023830)
#define GPIOA_IDR   REG(0x40020010) // the blue button is pin PA0 (1 = pressed)

#define SYST_CSR REG(0xE000E010)
#define SYST_RVR REG(0xE000E014)
#define SYST_CVR REG(0xE000E018)

#define PANEL_BG RGB(12, 14, 22)
#define BAR_BG   RGB(45, 50, 62)

static Tank tank;
static uint16_t water[TANK_ROWS]; // the water colour of each row (lighter at the top)

// How each fish looks (the tank's rules don't care about this part).
static struct {
  uint8_t tint[3];        // its own colour: red, green, blue
  uint8_t marking;        // 0 plain, 1 a stripe across the middle, 2 a dark back
  char name[4];
  uint32_t tag[TAG_H];    // its nametag as a picture: one row of dots per number
  uint16_t tag_color;
} look[N_FISH];

typedef struct { int x0, y0, x1, y1; } Rect;

// What is needed to draw each fish this frame, and to wipe where it was last frame.
static struct {
  float stretch_u, stretch_v; // turn screen distances into "standard fish" distances
  uint16_t paint[4];          // body, eye, tail, marking
  int tag_x, tag_y;           // where the nametag goes
  Rect now, last;             // the rectangle holding the fish and its nametag
  float last_health;
  int flash;                  // frames left of the red "ouch" flash
} art[N_FISH];

static struct { int x, y, shown; } crumb[N_FOOD];

// What each fish's box in the panel currently shows, so only changes get redrawn.
static struct { int ready, health, hunger, friend_of, enemy_of; } box[N_FISH];

static char log_text[LOG_LINES][LOG_CHARS + 1];
static int log_changed;
static uint32_t born;
static int next_name;

static const uint8_t STARTING_TINTS[N_FISH][3] = {
  {255, 140, 0},  {255, 90, 90},   {255, 210, 60},  {120, 230, 120}, {90, 200, 255},
  {200, 140, 255}, {255, 150, 200}, {240, 240, 240}, {160, 255, 220}, {180, 220, 90},
};
static const char NAMES[][4] = {
  "ADA", "BOB", "CAL", "DOT", "EVE", "FIN", "GUS", "HAL", "IVY", "JAX", "KIT", "LEO", "MAX",
  "NED", "OAK", "PIP", "RAY", "SAM", "TAD", "UMA", "VIC", "WES", "XAN", "YAZ", "ZOE",
};
#define N_NAMES ((int)(sizeof NAMES / sizeof NAMES[0]))

// Give fish i the next name on the list, and make its nametag picture from the font.
static void give_name(int i) {
  for (int c = 0; c < 4; c++) look[i].name[c] = NAMES[next_name][c];
  next_name = (next_name + 1) % N_NAMES;
  for (int row = 0; row < TAG_H; row++) {
    uint32_t dots = 0;
    for (int c = 0; c < 3; c++) {
      const char *glyph = font_glyph(look[i].name[c]);
      for (int col = 0; glyph && col < FONT_COLS; col++)
        if (glyph[row * FONT_COLS + col] == 'X') dots |= 1u << (c * 6 + col);
    }
    look[i].tag[row] = dots;
  }
}

// The nametag is a pale version of the fish's own colour.
static void tint_changed(int i) {
  look[i].tag_color = RGB((look[i].tint[0] + 255) / 2, (look[i].tint[1] + 255) / 2, (look[i].tint[2] + 255) / 2);
}

// ---------- the log ----------

static void log_add(const char *who, const char *did, const char *whom) {
  for (int l = 0; l < LOG_LINES - 1; l++)
    for (int c = 0; c <= LOG_CHARS; c++) log_text[l][c] = log_text[l + 1][c];
  char *out = log_text[LOG_LINES - 1];
  int n = 0;
  const char *parts[3] = {who, did, whom};
  for (int p = 0; p < 3; p++)
    for (const char *s = parts[p]; s && *s && n < LOG_CHARS; s++) out[n++] = *s;
  while (n < LOG_CHARS) out[n++] = ' '; // pad, so a short line wipes a longer old one
  out[n] = 0;
  log_changed = 1;
}

static void log_draw(void) {
  for (int l = 0; l < LOG_LINES; l++)
    lcd_text(0, LOG_Y + l * 8, log_text[l], 1, l == LOG_LINES - 1 ? WHITE : RGB(130, 140, 160), BLACK);
  log_changed = 0;
}

// ---------- drawing the fish ----------

// Which part of a standard-sized fish is at this spot? Measured from its middle:
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

// The colour of fish i's body at standard-fish spot (u, v), or -1 if that spot is water.
static inline int body_color(int i, float u, float v) {
  int part = fish_part(u, v);
  if (!part) return -1;
  if (part == 1) {
    if (look[i].marking == 1 && u > -0.4f && u < 1.2f) return art[i].paint[3];
    if (look[i].marking == 2 && v < -0.9f) return art[i].paint[3];
  }
  return art[i].paint[part - 1];
}

// The colour of fish i's nametag at screen dot (px, py), or -1 if the tag isn't lit there.
static inline int tag_color(int i, int px, int py) {
  unsigned tx = (unsigned)(px - art[i].tag_x), ty = (unsigned)(py - art[i].tag_y);
  if (tx < TAG_W && ty < TAG_H && ((look[i].tag[ty] >> tx) & 1u)) return look[i].tag_color;
  return -1;
}

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Put plain water back over a rectangle.
static void paint_water(Rect r) {
  r.x0 = clamp_int(r.x0, 0, LCD_W - 1); r.x1 = clamp_int(r.x1, 0, LCD_W - 1);
  r.y0 = clamp_int(r.y0, 0, TANK_ROWS - 1); r.y1 = clamp_int(r.y1, 0, TANK_ROWS - 1);
  lcd_area(r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
  for (int y = r.y0; y <= r.y1; y++)
    for (int x = r.x0; x <= r.x1; x++) lcd_push(water[y]);
  lcd_done();
}

// Work out how fish i should be drawn this frame: its size and shape, where its nametag
// goes, and its colours (its own tint, fading towards grey as it gets hungry, flashing
// red when bitten).
static void prepare(int i) {
  const Fish *f = &tank.fish[i];
  art[i].stretch_u = 1.0f / (f->size * f->slim);
  art[i].stretch_v = f->slim / f->size;

  int cx = (int)f->x, cy = (int)f->y;
  int reach = (int)(8.5f * f->size * f->slim) + 2; // the fish fits in a square this far around its middle
  // The nametag sits above the fish, or below it when the fish is at the top of the tank.
  int above = cy - reach - TAG_H - 1 >= 0;
  art[i].tag_x = clamp_int(cx - TAG_W / 2, 0, LCD_W - TAG_W);
  art[i].tag_y = above ? cy - reach - TAG_H - 1 : cy + reach + 2;
  Rect r = {cx - reach, above ? art[i].tag_y : cy - reach, cx + reach, above ? cy + reach : art[i].tag_y + TAG_H - 1};
  if (art[i].tag_x < r.x0) r.x0 = art[i].tag_x;
  if (art[i].tag_x + TAG_W - 1 > r.x1) r.x1 = art[i].tag_x + TAG_W - 1;
  art[i].now = r;

  if (f->health < art[i].last_health - 0.2f) art[i].flash = 8;
  art[i].last_health = f->health;

  float glow = 0.45f + 0.55f * f->energy;
  int r_ = (int)((float)look[i].tint[0] * glow), g = (int)((float)look[i].tint[1] * glow), b = (int)((float)look[i].tint[2] * glow);
  if (art[i].flash) { art[i].flash--; r_ = 255; g = 40; b = 40; }
  art[i].paint[0] = RGB(r_, g, b);
  art[i].paint[1] = RGB(10, 10, 30);
  art[i].paint[2] = RGB(r_ * 2 / 3, g * 2 / 3, b * 2 / 3);
  art[i].paint[3] = RGB(r_ * 2 / 5, g * 2 / 5, b * 2 / 5);
}

static int overlap(Rect a, Rect b) { return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1; }

// Draw fish i and its nametag where they are now, wiping where they were, in one go
// (so nothing flickers).
static void draw_fish(int i) {
  const Fish *f = &tank.fish[i];
  Rect area = art[i].now, was = art[i].last;

  if (was.x0 > area.x1 + 4 || area.x0 > was.x1 + 4 || was.y0 > area.y1 + 4 || area.y0 > was.y1 + 4) {
    paint_water(was); // it jumped (a newborn appearing elsewhere): wipe the old spot separately
  } else {            // otherwise one rectangle covers both the old spot and the new
    if (was.x0 < area.x0) area.x0 = was.x0;
    if (was.y0 < area.y0) area.y0 = was.y0;
    if (was.x1 > area.x1) area.x1 = was.x1;
    if (was.y1 > area.y1) area.y1 = was.y1;
  }
  area.x0 = clamp_int(area.x0, 0, LCD_W - 1); area.x1 = clamp_int(area.x1, 0, LCD_W - 1);
  area.y0 = clamp_int(area.y0, 0, TANK_ROWS - 1); area.y1 = clamp_int(area.y1, 0, TANK_ROWS - 1);

  // Other fish (or their nametags) inside this rectangle get drawn in it too.
  int near[5], n_near = 0;
  for (int j = 0; j < N_FISH && n_near < 5; j++)
    if (j != i && overlap(area, art[j].now)) near[n_near++] = j;

  const float su = art[i].stretch_u, sv = art[i].stretch_v;
  const float u_step = f->dx * su, v_step = -f->dy * sv; // one dot to the right on screen
  lcd_area(area.x0, area.y0, area.x1 - area.x0 + 1, area.y1 - area.y0 + 1);
  for (int py = area.y0; py <= area.y1; py++) {
    float ry = ((float)py + 0.5f) - f->y;
    float rx = ((float)area.x0 + 0.5f) - f->x;
    float u = (rx * f->dx + ry * f->dy) * su; // towards the nose
    float v = (ry * f->dx - rx * f->dy) * sv; // towards its right
    for (int px = area.x0; px <= area.x1; px++) {
      int color = body_color(i, u, v);
      if (color < 0) color = tag_color(i, px, py);
      for (int k = 0; color < 0 && k < n_near; k++) {
        int j = near[k];
        const Fish *o = &tank.fish[j];
        float qx = ((float)px + 0.5f) - o->x, qy = ((float)py + 0.5f) - o->y;
        color = body_color(j, (qx * o->dx + qy * o->dy) * art[j].stretch_u, (qy * o->dx - qx * o->dy) * art[j].stretch_v);
        if (color < 0) color = tag_color(j, px, py);
      }
      lcd_push(color < 0 ? water[py] : (uint16_t)color);
      u += u_step;
      v += v_step;
    }
  }
  lcd_done();
  art[i].last = art[i].now;
}

static void draw_food(void) {
  for (int k = 0; k < N_FOOD; k++) {
    const Food *food = &tank.food[k];
    int x = (int)food->x, y = (int)food->y;
    if (crumb[k].shown && (!food->there || crumb[k].x != x || crumb[k].y != y)) {
      Rect r = {crumb[k].x - 1, crumb[k].y - 1, crumb[k].x + 1, crumb[k].y + 1};
      paint_water(r);
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

// ---------- the panel under the tank ----------

static uint16_t tint_color(int i) { return RGB(look[i].tint[0], look[i].tint[1], look[i].tint[2]); }

// A little square in fish `who`'s colour, or an empty slot if who is -1.
static void swatch(int x, int y, int who) {
  if (who < 0) {
    lcd_fill_rect(x, y, 8, 8, BAR_BG);
    lcd_fill_rect(x + 1, y + 1, 6, 6, PANEL_BG);
  } else {
    lcd_fill_rect(x, y, 8, 8, tint_color(who));
  }
}

static void bar(int x, int y, int filled, uint16_t color) {
  if (filled > 0) lcd_fill_rect(x, y, filled, 4, color);
  if (filled < BAR_W) lcd_fill_rect(x + filled, y, BAR_W - filled, 4, BAR_BG);
}

// Bring fish i's box up to date, redrawing only what changed.
static void draw_box(int i) {
  const Fish *f = &tank.fish[i];
  int x = (i % 5) * CELL_W, y = TANK_ROWS + (i / 5) * CELL_H;

  if (!box[i].ready) {
    lcd_fill_rect(x, y, CELL_W, CELL_H, PANEL_BG);
    swatch(x + 3, y + 2, i);
    lcd_text(x + 14, y + 2, look[i].name, 1, WHITE, PANEL_BG);
    lcd_text(x + 3, y + 23, "+", 1, WHITE, PANEL_BG);
    lcd_text(x + 25, y + 23, "-", 1, WHITE, PANEL_BG);
    box[i].ready = 1;
    box[i].health = box[i].hunger = -1;
    box[i].friend_of = box[i].enemy_of = -2;
  }

  int health = (int)(f->health * (float)BAR_W + 0.5f);
  if (health != box[i].health) {
    bar(x + 3, y + 12, health, f->health < 0.35f ? RGB(230, 60, 50) : RGB(70, 200, 90));
    box[i].health = health;
  }
  int hunger = (int)((1.0f - f->energy) * (float)BAR_W + 0.5f);
  if (hunger != box[i].hunger) {
    bar(x + 3, y + 18, hunger, RGB(255, 150, 40));
    box[i].hunger = hunger;
  }

  // Best friend and worst enemy (only feelings strong enough to count).
  int best = -1, worst = -1;
  float best_like = 0.15f, worst_like = -0.15f;
  for (int j = 0; j < N_FISH; j++) {
    if (j == i) continue;
    float feeling = tank.like[i][j];
    if (feeling > best_like) { best_like = feeling; best = j; }
    if (feeling < worst_like) { worst_like = feeling; worst = j; }
  }
  if (best != box[i].friend_of) { swatch(x + 10, y + 23, best); box[i].friend_of = best; }
  if (worst != box[i].enemy_of) { swatch(x + 32, y + 23, worst); box[i].enemy_of = worst; }
}

static void show_number(int x, uint32_t n) {
  char digits[10], text[5];
  int count = 0, i = 0;
  do { digits[count++] = (char)('0' + n % 10); n /= 10; } while (n);
  while (count && i < 4) text[i++] = digits[--count];
  while (i < 4) text[i++] = ' ';
  text[i] = 0;
  lcd_text(x, COUNT_Y, text, 1, YELLOW, BLACK);
}

// ---------- what happened this tick ----------

// A baby has taken a dead fish's place in the tank's rules; now give it its looks.
static void dress_baby(int baby, int parent) {
  for (int c = 0; c < 3; c++) { // like its parent, in a slightly different shade
    int shade = (int)look[parent].tint[c] + (int)(rng_next(&tank.rng) % 61u) - 30;
    look[baby].tint[c] = (uint8_t)clamp_int(shade, 70, 255);
  }
  look[baby].marking = (rng_next(&tank.rng) % 8u) ? look[parent].marking : (uint8_t)(rng_next(&tank.rng) % 3u);
  give_name(baby);
  tint_changed(baby);

  art[baby].last_health = 1.0f;
  art[baby].flash = 0;
  for (int j = 0; j < N_FISH; j++) box[j].friend_of = box[j].enemy_of = -2; // colours changed: refresh
  box[baby].ready = 0;
  born++;
}

static void report(const Event *e) {
  const char *a = look[e->a].name, *b = look[e->b].name;
  switch (e->type) {
    case EV_BIT:     log_add(a, " BIT ", b); break;
    case EV_ATE:     log_add(a, " ATE ", b); break;
    case EV_STARVED: log_add(a, " STARVED", 0); break;
    case EV_LIKES:   log_add(a, " NOW LIKES ", b); break;
    case EV_HATES:   log_add(a, " NOW HATES ", b); break;
    case EV_BORN:
      dress_baby(e->a, e->b); // the dead fish's name is used up to here; now the baby gets its own
      log_add(look[e->a].name, " WAS BORN TO ", b);
      break;
  }
}

int main(void) {
  board_fast_clock();
  leds_init();
  led_red(1);
  lcd_init();
  int has_touch = touch_init();
  RCC_AHB1ENR |= 1u << 0; // port A, for the button

  // Water: lighter blue at the top, darker at the bottom.
  for (int y = 0; y < TANK_ROWS; y++) {
    int r = 8 - 6 * y / TANK_ROWS, g = 60 - 44 * y / TANK_ROWS, b = 110 - 62 * y / TANK_ROWS;
    water[y] = RGB(r, g, b);
  }
  Rect whole_tank = {0, 0, LCD_W - 1, TANK_ROWS - 1};
  paint_water(whole_tank);
  lcd_fill_rect(0, TANK_ROWS, LCD_W, LCD_H - TANK_ROWS, BLACK);
  lcd_text(2, COUNT_Y, "FED:", 1, WHITE, BLACK);
  lcd_text(62, COUNT_Y, "BORN:", 1, WHITE, BLACK);
  lcd_text(128, COUNT_Y, "EATEN:", 1, WHITE, BLACK);
  log_add(has_touch ? "TAP THE WATER TO FEED" : "BLUE BUTTON DROPS FOOD", 0, 0);
  log_add("GREEN: HEALTH   ORANGE: HUNGER", 0, 0);
  log_add("+ BEST FRIEND   - WORST ENEMY", 0, 0);

  tank_init(&tank, 0x1234ABCDu);
  for (int i = 0; i < N_FISH; i++) {
    for (int w = 0; w < N_WEIGHTS; w++) tank.fish[i].brain.w[w] = FISH_BRAINS[i][w];
    for (int c = 0; c < 3; c++) look[i].tint[c] = STARTING_TINTS[i][c];
    look[i].marking = (uint8_t)(i % 3);
    give_name(i);
    tint_changed(i);
    art[i].last_health = tank.fish[i].health;
    prepare(i);
    art[i].last = art[i].now;
  }
  led_red(0);

  // A free-running countdown, used to keep a steady 30 pictures a second.
  const uint32_t frame_ticks = cpu_hz / 30;
  SYST_RVR = 0xFFFFFF;
  SYST_CVR = 0;
  SYST_CSR = 5;

  uint32_t shown_fed = ~0u, shown_born = ~0u, shown_eaten = ~0u, frame = 0, last_feed = 0;
  int button_was = 0, touch_was = 0;
  for (;;) {
    uint32_t frame_start = SYST_CVR;

    // Feeding: the blue button drops food somewhere; a tap drops it right there.
    int button = (int)(GPIOA_IDR & 1u);
    if (button && !button_was) {
      tank.rng ^= SYST_CVR | 1u; // the moment you press makes the spot unpredictable
      tank_drop_food(&tank);
    }
    button_was = button;

    int tx = 0, ty = 0;
    int touching = has_touch && touch_read(&tx, &ty) && ty < TANK_ROWS;
    if (touching && (!touch_was || frame - last_feed >= 8)) { // hold to keep sprinkling
      tank_add_food(&tank, (float)tx, (float)ty);
      last_feed = frame;
    }
    touch_was = touching;

    tank_step(&tank);
    for (int i = 0; i < N_FISH; i++) {
      tank.fish[i].score *= 0.9995f; // old successes slowly count for less
      if (tank.fish[i].dead) tank_replace(&tank, i); // the best fish has a baby in its place
    }
    for (int e = 0; e < tank.n_events; e++) report(&tank.events[e]);

    for (int i = 0; i < N_FISH; i++) prepare(i);
    for (int i = 0; i < N_FISH; i++) draw_fish(i);
    draw_food();
    draw_box((int)(frame % N_FISH)); // one fish's box per frame is plenty
    if (log_changed) log_draw();

    if (tank.total_eaten != shown_fed) { shown_fed = tank.total_eaten; show_number(26, shown_fed); }
    if (born != shown_born) { shown_born = born; show_number(92, shown_born); }
    if (tank.total_killed != shown_eaten) { shown_eaten = tank.total_killed; show_number(164, shown_eaten); }

    led_green((int)((++frame / 15) & 1u)); // heartbeat

    while (((frame_start - SYST_CVR) & 0xFFFFFFu) < frame_ticks) {}
  }
}
