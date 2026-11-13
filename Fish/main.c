// Fish: a small ecosystem. Ten fish, each steered by its own neural-network brain, live
// over a seafloor where plants grow and feed the tank. (The rules are in fish_sim.c.)
//
//   Seafloor       Sand, rocks and plants. Plants grow (faster in rich water) and release
//                  food. Small fish also nibble the plants. Dead fish enrich the water.
//   Evolving       Everyone starts as a tiny Fry. Eating earns meal points; enough of them
//                  and an animal evolves: Fry, Squid, Turtle, then Shark and Orca (if it
//                  has been a hunter) or Ray and Whale (if it has been gentle). Each is
//                  bigger and stronger. Every bite is a meal too.
//   Hunting        A hungry fish hunts clearly weaker fish. The bitten fish flashes red.
//   Fighting       Enemies attack each other whatever their size; both get hurt.
//   Relationships  Fish that swim together become friends; crowding, stealing food and
//                  attacks make enemies. Friends never attack each other.
//   Nametags       Every fish carries its name above it.
//   Feeding        Tap the water to drop food there. The blue button drops some at random.
//   Panel          A box per fish: name and evolution level (1-5), green bar = health,
//                  orange bar = hunger, the colours of its best friend (+) and worst enemy (-).
//   Log            Three lines reporting what happens, newest at the bottom.
//   Genetics       Every animal has a genome (speed, shape, build, thrift, colour,
//                  pattern, five personality traits, and its brain). When one dies, the animal doing best
//                  has a baby with a partner; the baby gets each gene from one parent or
//                  the other, with the odd mutation. So families look alike, and the tank
//                  evolves. Each animal belongs to the family of one of the ten founders.
//   Website        The website can show this very tank, live, over the USB cable (and
//                  drop food into it). See "fish_mirror" below.

#include "board.h"
#include "lcd.h"
#include "font.h"
#include "touch.h"
#include "fish_sim.h"
#include "fish_view.h"
#include "fish_brains.h"
#include "fish_talk.h"

// Screen layout, top to bottom
#define TANK_ROWS 224 // the water and seafloor
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

// ---------- the website's window into this tank ----------
// Everything the website needs to draw this tank is copied here every frame. The website
// reads this block of memory straight out of the running chip through the same USB cable
// used for flashing, and can ask for food by writing to the "feed" numbers.
#define MIRROR_LOG 8
#define HISTORY 200
typedef struct {
  uint16_t id, mum, dad, gen;
  uint8_t family, kind, r, g, b, marking, dead, spare;
  char name[4];
} Ancestor; // 20 bytes (the website reads them by this layout)
struct Mirror {
  uint32_t magic;            // spells "FISH", so the website knows it found the right thing
  uint32_t version;
  volatile uint32_t feed_count; // the website adds 1 to this after filling in where:
  volatile float feed_x, feed_y;
  uint32_t seq;              // goes up by 1 before and after every update (odd = mid-update)
  float view[VIEW_SIZE];     // the tank, laid out as in fish_view.h
  uint8_t looks[N_FISH][8];  // per fish: red, green, blue, marking, 3 letters of name, flashing
  uint32_t log_count;        // how many log lines there have ever been
  char log[MIRROR_LOG][LOG_CHARS]; // the most recent ones, going round and round
  uint32_t seq_end;          // same as seq once an update is complete
  // The family history: every founder, and the most recent animals born after them, so
  // the website can draw this tank's whole family tree (not just who is alive now).
  uint32_t history_count;
  Ancestor history[HISTORY];
  // Where everyone is right now, on its own: small enough for the website to read many
  // times a second, so the tank it shows moves smoothly. Updated every frame drawn.
  uint32_t motion_seq;
  float motion[N_FISH][4];   // x, y, dx, dy, exactly as drawn on the screen
  uint32_t motion_seq_end;
};
struct Mirror fish_mirror;

// ---------- looks ----------

// How each fish looks (the tank's rules don't care about this part).
// (Its colour and pattern are genes: tank.fish[i].g.)
static struct {
  char name[4];
  uint32_t tag[TAG_H];    // its nametag as a picture: one row of dots per number
  uint16_t tag_color;
} look[N_FISH];

typedef struct { int x0, y0, x1, y1; } Rect;

// What is needed to draw each fish this frame, and to wipe where it was last frame.
static struct {
  // The rules move the fish 30 times a second, but the screen is redrawn as often as the
  // chip can manage. In between, each fish is drawn part-way from where it was at the
  // last tick (old_...) to where it is now, which is what makes the movement smooth.
  float x, y, dx, dy;                 // where it is drawn this frame
  float old_x, old_y, old_dx, old_dy; // where it was one tick ago
  float stretch_u, stretch_v; // turn screen distances into "standard fish" distances
  uint16_t paint[5];          // body, eye, fins and tail, dark marking, pale parts
  int tag_x, tag_y;           // where the nametag goes
  Rect now, last;             // the rectangle holding the fish and its nametag
  float last_health;
  int flash;                  // frames left of the red "ouch" flash
} art[N_FISH];

static struct { int x, y, shown; } crumb[N_FOOD];

// What each fish's box in the panel currently shows, so only changes get redrawn.
static struct { int ready, health, most, hunger, friend_of, enemy_of, stage; } box[N_FISH];

static char log_text[LOG_LINES][LOG_CHARS + 1];
static int log_changed;
static uint32_t born;
static int next_name;

static const char NAMES[][4] = {
  "ADA", "BOB", "CAL", "DOT", "EVE", "FIN", "GUS", "HAL", "IVY", "JAX", "KIT", "LEO", "MAX",
  "NED", "OAK", "PIP", "RAY", "SAM", "TAD", "UMA", "VIC", "WES", "XAN", "YAZ", "ZOE",
};
#define N_NAMES ((int)(sizeof NAMES / sizeof NAMES[0]))
static const char *const SPECIES[N_KINDS] = {"FRY", "SQUID", "TURTLE", "SHARK", "RAY", "ORCA", "WHALE"};

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Give fish i the next name on the list, and make its nametag picture from the font.
static void give_name(int i) {
  for (int c = 0; c < 4; c++) look[i].name[c] = NAMES[next_name][c];
  talk_set_name(i, look[i].name);
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
  const Genome *g = &tank.fish[i].g;
  look[i].tag_color = RGB((g->r + 255) / 2, (g->g + 255) / 2, (g->b + 255) / 2);
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

  char *copy = fish_mirror.log[fish_mirror.log_count % MIRROR_LOG]; // and for the website
  for (int c = 0; c < LOG_CHARS; c++) copy[c] = out[c];
  fish_mirror.log_count++;
}

static void log_draw(void) {
  for (int l = 0; l < LOG_LINES; l++)
    lcd_text(0, LOG_Y + l * 8, log_text[l], 1, l == LOG_LINES - 1 ? WHITE : RGB(130, 140, 160), BLACK);
  log_changed = 0;
}

// ---------- the scenery: water, seafloor, rocks, plants ----------

static uint16_t water[TANK_ROWS];  // the water colour of each row (lighter at the top)
static uint8_t floor_top[LCD_W];   // for each column, the row where the seafloor starts
static uint8_t plant_col[LCD_W];   // for each column, the row where a plant blade starts (255 = no plant)
static int plant_shown[N_PLANTS];  // how tall each plant was last drawn, in dots

static const struct { int x, y, r; } ROCKS[] = {{40, 214, 9}, {150, 215, 7}, {205, 214, 10}};
#define N_ROCKS ((int)(sizeof ROCKS / sizeof ROCKS[0]))
// Each plant is three blades: how far each is from the plant's middle, and how tall.
static const int BLADE_AT[3] = {-4, 0, 4};
static const float BLADE_TALL[3] = {0.7f, 1.0f, 0.85f};

static int zigzag(int x, int period) { // 0 up to period/2 and back down, over and over
  int v = x % period - period / 2;
  return v < 0 ? -v : v;
}

static uint16_t floor_color(int px, int py) {
  for (int k = 0; k < N_ROCKS; k++) {
    int ox = px - ROCKS[k].x, oy = py - ROCKS[k].y;
    if (ox * ox + oy * oy < ROCKS[k].r * ROCKS[k].r)
      return oy < -ROCKS[k].r / 2 ? RGB(150, 155, 165) : RGB(105, 110, 122); // lit on top
  }
  return (px * 13 + py * 7) % 11 == 0 ? RGB(160, 136, 84) : RGB(196, 172, 112); // speckled sand
}

// ---------- speech bubbles ----------
// A bubble appears over an animal while it talks: "..." while the model reads what's going
// on, then the words as they are written, then it stays a few seconds. Bubbles sit on top
// of everything in the tank; they stay where they appeared (animals swim underneath).
#define BUB_COLS 17                    // characters per line (two lines: 34, a whole line of talk)
#define BUB_W (BUB_COLS * 6 + 5)       // 107 dots wide
#define BUB_H 21                       // 2 lines of 8 dots, plus edges
#define BUBBLES 3
#define BUB_FILL RGB(245, 245, 238)
#define BUB_INK  RGB(20, 24, 36)
#define BUB_EDGE RGB(120, 130, 150)
typedef struct {
  int on;
  Rect r;
  int speaker;
  uint16_t id;
  uint32_t until;            // tick it disappears (0 = still being written)
  char text[TALK_LINE + 1];
  const char *cell[2][BUB_COLS]; // each character's picture from the font, or 0 for a space
} Bubble;
static Bubble bub[BUBBLES];
static int bubbles_on;

// The colour of a bubble at screen dot (px, py), or -1 if no bubble is there.
static inline int bubble_color(int px, int py) {
  for (int k = 0; k < BUBBLES; k++) {
    const Bubble *b = &bub[k];
    if (!b->on || px < b->r.x0 || px > b->r.x1 || py < b->r.y0 || py > b->r.y1) continue;
    int lx = px - b->r.x0, ly = py - b->r.y0;
    int edge_x = lx == 0 || lx == BUB_W - 1, edge_y = ly == 0 || ly == BUB_H - 1;
    if (edge_x && edge_y) continue; // rounded corners: see through
    if (edge_x || edge_y) return BUB_EDGE;
    int tx = lx - 3, ty = ly - 3;
    if (tx < 0 || ty < 0 || tx >= BUB_COLS * 6 || ty >= 16) return BUB_FILL;
    const char *glyph = b->cell[ty / 8][tx / 6];
    int gx = tx % 6, gy = ty % 8;
    return glyph && gx < FONT_COLS && gy < FONT_ROWS && glyph[gy * FONT_COLS + gx] == 'X' ? BUB_INK : BUB_FILL;
  }
  return -1;
}

// What is behind the fish at this dot: a speech bubble, seafloor, a plant, or water.
static inline uint16_t background(int px, int py) {
  if (bubbles_on) { int c = bubble_color(px, py); if (c >= 0) return (uint16_t)c; }
  if (py >= floor_top[px]) return floor_color(px, py);
  for (int k = 0; k < N_OBSTACLES; k++) { // the rocks the animals swim around
    float ox = (float)px + 0.5f - OBSTACLES[k][0], oy = (float)py + 0.5f - OBSTACLES[k][1], r = OBSTACLES[k][2];
    if (ox * ox + oy * oy > r * r) continue;
    unsigned speck = (unsigned)(px * 37 + py * 11) % 13u;
    if (oy < -0.55f * r && speck < 5) return RGB(62, 118, 72);           // moss on top
    if (ox + oy < -0.45f * r) return speck == 0 ? RGB(110, 118, 132) : RGB(140, 148, 162); // lit side
    return speck < 2 ? RGB(64, 70, 84) : RGB(96, 103, 118);
  }
  if (py >= plant_col[px]) return (((py >> 2) + px) & 1) ? RGB(40, 150, 70) : RGB(28, 112, 58);
  return water[py];
}

// Put the scenery back over a rectangle.
static void paint_background(Rect r) {
  r.x0 = clamp_int(r.x0, 0, LCD_W - 1); r.x1 = clamp_int(r.x1, 0, LCD_W - 1);
  r.y0 = clamp_int(r.y0, 0, TANK_ROWS - 1); r.y1 = clamp_int(r.y1, 0, TANK_ROWS - 1);
  lcd_area(r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
  for (int y = r.y0; y <= r.y1; y++)
    for (int x = r.x0; x <= r.x1; x++) lcd_push(background(x, y));
  lcd_done();
}

// The water's colours for how light it is (1 = midday): lighter blue at the top, darker
// lower down, and darker all over at night.
static void set_water(float light) {
  float k = 0.3f + 0.7f * light;
  for (int y = 0; y < TANK_ROWS; y++) {
    int r = 8 - 6 * y / TANK_ROWS, g = 60 - 44 * y / TANK_ROWS, b = 110 - 62 * y / TANK_ROWS;
    water[y] = RGB((int)((float)r * k), (int)((float)g * k), (int)((float)b * (0.25f + 0.75f * k)));
  }
}

static void scenery_init(void) {
  set_water(1.0f);
  for (int x = 0; x < LCD_W; x++) {
    int top = 212 - zigzag(x, 37) / 6 - zigzag(x + 5, 13) / 4; // gently uneven sand
    for (int k = 0; k < N_ROCKS; k++) {                       // rocks poke up out of it
      int ox = x - ROCKS[k].x;
      if (ox > -ROCKS[k].r && ox < ROCKS[k].r) {
        int rise = 0;
        while ((rise + 1) * (rise + 1) + ox * ox < ROCKS[k].r * ROCKS[k].r) rise++;
        if (ROCKS[k].y - rise < top) top = ROCKS[k].y - rise;
      }
    }
    floor_top[x] = (uint8_t)top;
    plant_col[x] = 255;
  }
  for (int p = 0; p < N_PLANTS; p++) plant_shown[p] = -1;
}

// Redraw any plant that has grown or been eaten since it was last drawn.
static void draw_plants(void) {
  for (int p = 0; p < N_PLANTS; p++) {
    int tall = (int)(tank.plants[p].height * PLANT_TALL);
    if (tall == plant_shown[p]) continue;
    plant_shown[p] = tall;
    for (int b = 0; b < 3; b++) {
      for (int w = 0; w < 2; w++) {
        int x = (int)tank.plants[p].x + BLADE_AT[b] + w;
        if (x < 0 || x >= LCD_W) continue;
        int blade = (int)((float)tall * BLADE_TALL[b]);
        int old_top = plant_col[x] == 255 ? floor_top[x] : plant_col[x];
        int new_top = floor_top[x] - blade;
        plant_col[x] = blade > 0 ? (uint8_t)clamp_int(new_top, 0, 254) : 255;
        Rect r = {x, old_top < new_top ? old_top : new_top, x, floor_top[x] - 1};
        if (r.y0 <= r.y1) paint_background(r);
      }
    }
  }
}

// ---------- drawing the fish ----------

// ---- the seven sea animals ----
// Each is described on a "standard animal" grid, measured from its middle:
// u = how far towards its nose, v = how far to its right. The answer is which part of
// the animal is at that spot:
//   0 nothing, 1 body, 2 eye, 3 fin or tail, 4 dark marking, 5 pale (belly, patches)

static inline float absf(float v) { return v < 0.0f ? -v : v; }

// Is (u, v) inside an oval centred on (cu, cv), ru long and rv wide?
static inline int oval(float u, float v, float cu, float cv, float ru, float rv) {
  float a = (u - cu) / ru, b = (v - cv) / rv;
  return a * a + b * b <= 1.0f;
}

static int animal_part(int kind, float u, float v) {
  const float av = absf(v); // distance from its centre line, either side
  switch (kind) {
    case K_FRY: // a tiny fish: oval body, simple tail
      if (oval(u, v, 1.0f, 0.0f, 4.0f, 2.2f)) return oval(u, v, 3.0f, -0.8f, 0.8f, 0.8f) ? 2 : 1;
      if (u < -2.5f && u >= -6.5f && av < 0.5f + (-2.5f - u) * 0.55f) return 3;
      return 0;

    case K_SQUID: // a long body with fins at the tip, and three arms trailing behind
      if (oval(u, v, 1.5f, 0.0f, 5.0f, 2.2f)) return oval(u, v, -2.2f, -1.0f, 0.8f, 0.8f) ? 2 : 1;
      if (u > 3.0f && u < 6.0f && av > 1.2f && av < 1.2f + (6.0f - u) * 0.9f) return 3;
      if (u < -3.0f && u >= -8.5f) {
        float from_arm = av > 0.7f ? absf(av - 1.4f) : av; // arms run along v = -1.4, 0 and 1.4
        if (from_arm < 0.45f) return 3;
      }
      return 0;

    case K_TURTLE: // a round shell with a dark middle, a pale head, four flippers
      if (oval(u, v, 0.0f, 0.0f, 4.2f, 3.6f)) return oval(u, v, 0.0f, 0.0f, 2.3f, 1.9f) ? 4 : 1;
      if (oval(u, v, 5.4f, 0.0f, 1.6f, 1.2f)) return oval(u, v, 5.9f, -0.5f, 0.5f, 0.5f) ? 2 : 5;
      if (oval(u, av, 2.8f, 4.3f, 1.9f, 1.2f)) return 3;
      if (oval(u, av, -3.2f, 3.6f, 1.4f, 0.9f)) return 3;
      if (u < -4.0f && u > -5.6f && av < 0.4f) return 3;
      return 0;

    case K_SHARK: // long and slim, pale belly, tall back fin, crescent tail
      if (oval(u, v, 0.5f, 0.0f, 7.0f, 2.3f)) {
        if (oval(u, v, 5.0f, -0.9f, 0.7f, 0.7f)) return 2;
        return v > 0.9f ? 5 : 1;
      }
      if (u > -1.0f && u < 2.5f && v < -2.0f && v > -2.0f - (2.5f - u) * 0.9f) return 3;
      if (u > 0.0f && u < 3.0f && v > 2.0f && v < 2.0f + (3.0f - u) * 0.6f) return 3;
      if (u < -5.5f && u >= -9.5f) {
        float back = -5.5f - u;
        if (av < 0.6f + back && av > back * 0.55f - 0.3f) return 3;
      }
      return 0;

    case K_RAY: // a wide diamond with a dark stripe down the middle and a thin tail
      if (absf(u - 0.5f) * (1.0f / 5.0f) + av * (1.0f / 5.5f) < 1.0f) {
        if (oval(u, av, 3.2f, 1.2f, 0.6f, 0.6f)) return 2;
        return (av < 0.6f && u < 3.0f) ? 4 : 1;
      }
      if (u < -4.0f && u >= -9.0f && av < 0.35f) return 3;
      return 0;

    case K_ORCA: // dark and heavy, with a pale belly and a pale patch behind the eye
      if (oval(u, v, 0.5f, 0.0f, 7.0f, 2.9f)) {
        if (oval(u, v, 5.3f, -0.5f, 0.5f, 0.5f)) return 2;
        if (oval(u, v, 4.0f, -1.3f, 1.2f, 0.55f)) return 5;
        return v > 1.3f ? 5 : 4;
      }
      if (u > -0.5f && u < 2.0f && v < -2.6f && v > -2.6f - (2.0f - u) * 1.2f) return 4;
      if (u > 0.5f && u < 3.0f && v > 2.6f && v < 2.6f + (3.0f - u) * 0.6f) return 4;
      if (u < -5.8f && u >= -9.5f) {
        float back = -5.8f - u;
        if (av < 0.5f + back && av > back * 0.45f - 0.4f) return 4;
      }
      return 0;

    default: // K_WHALE: the biggest: a great rounded body, grooved pale belly, wide tail
      if (oval(u, v, 0.5f, 0.0f, 7.5f, 3.4f)) {
        if (oval(u, v, 5.2f, -0.5f, 0.55f, 0.55f)) return 2;
        if (v > 1.4f) return (((int)(u * 1.4f + 20.0f)) & 1) ? 5 : 3;
        return 1;
      }
      if (u > 1.0f && u < 3.5f && v > 3.0f && v < 3.0f + (3.5f - u) * 0.5f) return 3;
      if (u < -6.2f && u >= -10.0f) {
        float back = -6.2f - u;
        if (av < 0.5f + back * 1.1f && av > back * 0.5f - 0.4f) return 3;
      }
      return 0;
  }
}

// How far each kind reaches from its middle on the standard grid: lengthways and sideways.
static const float REACH_ALONG[N_KINDS]  = {6.5f, 8.5f, 7.0f, 9.5f, 9.0f, 9.5f, 10.0f};
static const float REACH_ACROSS[N_KINDS] = {2.8f, 3.5f, 5.5f, 5.2f, 5.5f, 5.6f, 4.9f};

// The colour of animal i at standard-grid spot (u, v), or -1 if nothing is there.
static inline int body_color(int i, float u, float v) {
  int part = animal_part(tank.fish[i].kind, u, v);
  if (!part) return -1;
  if (part == 1) { // its own markings go on plain body only
    if (tank.fish[i].g.marking == 1 && u > -0.4f && u < 1.2f) return art[i].paint[3];
    if (tank.fish[i].g.marking == 2 && v < -0.9f) return art[i].paint[3];
  }
  return art[i].paint[part - 1];
}

// The colour of fish i's nametag at screen dot (px, py), or -1 if the tag isn't lit there.
static inline int tag_color(int i, int px, int py) {
  unsigned tx = (unsigned)(px - art[i].tag_x), ty = (unsigned)(py - art[i].tag_y);
  if (tx < TAG_W && ty < TAG_H && ((look[i].tag[ty] >> tx) & 1u)) return look[i].tag_color;
  return -1;
}

// Once per tick of the rules: notice a fish being hurt (it flashes red for a moment).
static void after_tick(int i) {
  const Fish *f = &tank.fish[i];
  if (f->health < art[i].last_health - 0.15f) art[i].flash = 8;
  else if (art[i].flash) art[i].flash--;
  art[i].last_health = f->health;
}

// Work out how fish i should be drawn this frame: where exactly (blend = 0 is where it was
// at the last tick, 1 is where it is now), its size and shape, where its nametag goes, and
// its colours (its own tint, fading towards grey as it gets hungry, red when hurt).
static void prepare(int i, float blend) {
  const Fish *f = &tank.fish[i];
  float jump_x = f->x - art[i].old_x, jump_y = f->y - art[i].old_y;
  if (jump_x > 8.0f || jump_x < -8.0f || jump_y > 8.0f || jump_y < -8.0f) blend = 1.0f; // a newborn elsewhere: no gliding across
  art[i].x = art[i].old_x + jump_x * blend;
  art[i].y = art[i].old_y + jump_y * blend;
  float hx = art[i].old_dx + (f->dx - art[i].old_dx) * blend, hy = art[i].old_dy + (f->dy - art[i].old_dy) * blend;
  float len = __builtin_sqrtf(hx * hx + hy * hy);
  if (len < 0.01f) { hx = f->dx; hy = f->dy; len = 1.0f; } // it turned right round: just snap
  art[i].dx = hx / len;
  art[i].dy = hy / len;

  art[i].stretch_u = 1.0f / (f->size * f->g.slim);
  art[i].stretch_v = f->g.slim / f->size;

  int cx = (int)art[i].x, cy = (int)art[i].y;
  // The animal fits in a square this far around its middle.
  float along = REACH_ALONG[f->kind] * f->size * f->g.slim, across = REACH_ACROSS[f->kind] * f->size / f->g.slim;
  int reach = (int)(along > across ? along : across) + 2;
  // The nametag sits above the fish, or below it when the fish is at the top of the tank.
  int above = cy - reach - TAG_H - 1 >= 0;
  art[i].tag_x = clamp_int(cx - TAG_W / 2, 0, LCD_W - TAG_W);
  art[i].tag_y = above ? cy - reach - TAG_H - 1 : cy + reach + 2;
  Rect r = {cx - reach, above ? art[i].tag_y : cy - reach, cx + reach, above ? cy + reach : art[i].tag_y + TAG_H - 1};
  if (art[i].tag_x < r.x0) r.x0 = art[i].tag_x;
  if (art[i].tag_x + TAG_W - 1 > r.x1) r.x1 = art[i].tag_x + TAG_W - 1;
  art[i].now = r;

  float glow = 0.45f + 0.55f * f->energy;
  int red = (int)((float)f->g.r * glow), green = (int)((float)f->g.g * glow), blue = (int)((float)f->g.b * glow);
  if (art[i].flash) { red = 255; green = 40; blue = 40; }
  art[i].paint[0] = RGB(red, green, blue);
  art[i].paint[1] = RGB(10, 10, 30);
  art[i].paint[2] = RGB(red * 2 / 3, green * 2 / 3, blue * 2 / 3);
  art[i].paint[3] = RGB(red * 2 / 5, green * 2 / 5, blue * 2 / 5);
  art[i].paint[4] = RGB((red + 2 * 255) / 3, (green + 2 * 255) / 3, (blue + 2 * 255) / 3);
}

static int overlap(Rect a, Rect b) { return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1; }

// Draw fish i and its nametag where they are now, wiping where they were, in one go
// (so nothing flickers).
static void draw_fish(int i) {
  const float fx = art[i].x, fy = art[i].y, fdx = art[i].dx, fdy = art[i].dy;
  Rect area = art[i].now, was = art[i].last;

  if (was.x0 > area.x1 + 4 || area.x0 > was.x1 + 4 || was.y0 > area.y1 + 4 || area.y0 > was.y1 + 4) {
    paint_background(was); // it jumped (a newborn appearing elsewhere): wipe the old spot separately
  } else {                 // otherwise one rectangle covers both the old spot and the new
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

  int under_bubble = 0; // only check bubbles where there might be one
  for (int k = 0; k < BUBBLES; k++) if (bub[k].on && overlap(area, bub[k].r)) under_bubble = 1;

  const float su = art[i].stretch_u, sv = art[i].stretch_v;
  const float u_step = fdx * su, v_step = -fdy * sv; // one dot to the right on screen
  lcd_area(area.x0, area.y0, area.x1 - area.x0 + 1, area.y1 - area.y0 + 1);
  for (int py = area.y0; py <= area.y1; py++) {
    float ry = ((float)py + 0.5f) - fy;
    float rx = ((float)area.x0 + 0.5f) - fx;
    float u = (rx * fdx + ry * fdy) * su; // towards the nose
    float v = (ry * fdx - rx * fdy) * sv; // towards its right
    for (int px = area.x0; px <= area.x1; px++) {
      int color = under_bubble ? bubble_color(px, py) : -1;
      if (color < 0) color = body_color(i, u, v);
      if (color < 0) color = tag_color(i, px, py);
      for (int k = 0; color < 0 && k < n_near; k++) {
        int j = near[k];
        float qx = ((float)px + 0.5f) - art[j].x, qy = ((float)py + 0.5f) - art[j].y;
        color = body_color(j, (qx * art[j].dx + qy * art[j].dy) * art[j].stretch_u, (qy * art[j].dx - qx * art[j].dy) * art[j].stretch_v);
        if (color < 0) color = tag_color(j, px, py);
      }
      lcd_push(color < 0 ? background(px, py) : (uint16_t)color);
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
      paint_background(r);
      crumb[k].shown = 0;
    }
    if (food->there && bubble_color(x, y) < 0) {
      lcd_fill_rect(x - 1, y - 1, 3, 3, RGB(255, 230, 120));
      crumb[k].x = x;
      crumb[k].y = y;
      crumb[k].shown = 1;
    }
  }
}

// Fill in a bubble's text: split into two lines at a space, and look up each character.
static void bubble_text(Bubble *b, const char *text) {
  int n = 0;
  while (text[n] && n < TALK_LINE) { b->text[n] = text[n]; n++; }
  b->text[n] = 0;
  int split = n;
  if (n > BUB_COLS) { // break at the last space that fits on the first line
    split = BUB_COLS;
    for (int k = BUB_COLS; k > 0; k--) if (text[k] == ' ') { split = k; break; }
  }
  for (int row = 0; row < 2; row++)
    for (int col = 0; col < BUB_COLS; col++) b->cell[row][col] = 0;
  for (int k = 0, col = 0; k < split && col < BUB_COLS; k++, col++) b->cell[0][col] = font_glyph(text[k]);
  int start = split < n && text[split] == ' ' ? split + 1 : split;
  for (int k = start, col = 0; k < n && col < BUB_COLS; k++, col++) b->cell[1][col] = font_glyph(text[k]);
}

static void bubble_redraw(const Bubble *b) { paint_background(b->r); }

// The bubble belonging to animal `who` (as it is now), or a free one, or the oldest.
static Bubble *bubble_for(int who) {
  Bubble *free_one = 0, *oldest = &bub[0];
  for (int k = 0; k < BUBBLES; k++) {
    Bubble *b = &bub[k];
    if (b->on && b->speaker == who && b->id == tank.fish[who].id) return b;
    if (!b->on && !free_one) free_one = b;
    if (b->on && b->until && (!oldest->on || !oldest->until || b->until < oldest->until)) oldest = b;
  }
  if (free_one) return free_one;
  oldest->on = 0;
  paint_background(oldest->r);
  return oldest;
}

// Show `text` over animal `who`; until = 0 while it is still being written.
static void bubble_say(int who, const char *text, uint32_t until) {
  Bubble *b = bubble_for(who);
  if (!b->on) { // place it above the animal and its nametag (or below, near the top)
    int x0 = clamp_int((int)art[who].x - BUB_W / 2, 0, LCD_W - BUB_W);
    int y0 = art[who].now.y0 - BUB_H - 2;
    if (y0 < 0) y0 = art[who].now.y1 + 2;
    y0 = clamp_int(y0, 0, TANK_ROWS - BUB_H);
    Rect r = {x0, y0, x0 + BUB_W - 1, y0 + BUB_H - 1};
    b->r = r;
    b->speaker = who;
    b->id = tank.fish[who].id;
    b->on = 1;
    b->text[0] = 1; // (forces the first draw)
  }
  b->until = until;
  int same = 1;
  for (int k = 0; same && (text[k] || b->text[k]); k++) same = text[k] == b->text[k];
  bubbles_on = 1;
  if (!same) { bubble_text(b, text); bubble_redraw(b); }
}

// Each tick: show what's being written right now, and take down bubbles whose time is up.
static void bubbles_tick(uint32_t ticks) {
  int speaker, listener;
  char so_far[TALK_LINE + 1];
  int speaking = talk_current(&speaker, &listener, so_far) ? speaker : -1;
  if (speaking >= 0) bubble_say(speaker, so_far[0] ? so_far : "...", 0);
  int any = 0;
  for (int k = 0; k < BUBBLES; k++) {
    Bubble *b = &bub[k];
    // A bubble still "being written" by someone who has stopped (or died): let it go soon.
    if (b->on && !b->until && (b->speaker != speaking || b->id != tank.fish[b->speaker].id)) b->until = ticks + 90;
    if (b->on && b->until && ticks >= b->until) { b->on = 0; paint_background(b->r); }
    any |= b->on;
  }
  bubbles_on = any;
}

// ---------- the panel under the tank ----------

static uint16_t tint_color(int i) { return RGB(tank.fish[i].g.r, tank.fish[i].g.g, tank.fish[i].g.b); }

// A little square in fish `who`'s colour, or an empty slot if who is -1.
static void swatch(int x, int y, int who) {
  if (who < 0) {
    lcd_fill_rect(x, y, 8, 8, BAR_BG);
    lcd_fill_rect(x + 1, y + 1, 6, 6, PANEL_BG);
  } else {
    lcd_fill_rect(x, y, 8, 8, tint_color(who));
  }
}

// A bar: `filled` dots in colour, then up to `room` dots in a dim colour (how full it could
// be), then the empty track.
static void bar(int x, int y, int filled, int room, uint16_t color, uint16_t dim) {
  filled = clamp_int(filled, 0, BAR_W);
  room = clamp_int(room, filled, BAR_W);
  if (filled > 0) lcd_fill_rect(x, y, filled, 4, color);
  if (room > filled) lcd_fill_rect(x + filled, y, room - filled, 4, dim);
  if (room < BAR_W) lcd_fill_rect(x + room, y, BAR_W - room, 4, BAR_BG);
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
    box[i].health = box[i].most = box[i].hunger = box[i].stage = -1;
    box[i].friend_of = box[i].enemy_of = -2;
  }

  if (f->stage != box[i].stage) { // its species level, 1 to 5, after the name
    char level[2] = {(char)('1' + f->stage), 0};
    lcd_text(x + 38, y + 2, level, 1, YELLOW, PANEL_BG);
    box[i].stage = f->stage;
  }
  // Health: the whole bar is the most health any fish can have. The bright part is this
  // fish's health now; the dim part is how much more it could have at its size. So a big
  // fish has a long bar and a small one a short bar.
  float most_health = fish_max_health(f);
  int health = (int)(f->health / HEALTH_LIMIT * (float)BAR_W + 0.5f);
  int most = (int)(most_health / HEALTH_LIMIT * (float)BAR_W + 0.5f);
  if (health != box[i].health || most != box[i].most) {
    bar(x + 3, y + 12, health, most, f->health < 0.35f * most_health ? RGB(230, 60, 50) : RGB(70, 200, 90), RGB(36, 78, 48));
    box[i].health = health;
    box[i].most = most;
  }
  int hunger = (int)((1.0f - f->energy) * (float)BAR_W + 0.5f);
  if (hunger != box[i].hunger) {
    bar(x + 3, y + 18, hunger, hunger, RGB(255, 150, 40), BAR_BG);
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

// ---------- the family history (for the website's family tree) ----------

static uint32_t history_next = N_FISH; // where the next newborn is written (founders keep 0..9)

static Ancestor *ancestor(uint16_t id) {
  for (uint32_t k = 0; k < fish_mirror.history_count; k++)
    if (fish_mirror.history[k].id == id) return &fish_mirror.history[k];
  return 0;
}

// Write animal i into the family history (the oldest non-founder is forgotten when full).
static void remember(int i) {
  const Fish *f = &tank.fish[i];
  uint32_t k = f->gen == 0 && fish_mirror.history_count < N_FISH ? fish_mirror.history_count : history_next;
  if (f->gen != 0 || fish_mirror.history_count >= N_FISH) {
    history_next = history_next + 1 >= HISTORY ? N_FISH : history_next + 1;
  }
  Ancestor *a = &fish_mirror.history[k];
  a->id = f->id; a->mum = f->mum; a->dad = f->dad; a->gen = f->gen;
  a->family = f->family; a->kind = f->kind; a->r = f->g.r; a->g = f->g.g; a->b = f->g.b;
  a->marking = f->g.marking; a->dead = 0; a->spare = 0;
  for (int c = 0; c < 4; c++) a->name[c] = look[i].name[c];
  if (k >= fish_mirror.history_count) fish_mirror.history_count = k + 1;
}

// A baby has taken a dead animal's place in the tank's rules (its colour and pattern come
// from its genes); now give it a name.
static void dress_baby(int baby) {
  give_name(baby);
  remember(baby);
  tint_changed(baby);

  art[baby].last_health = 1.0f;
  art[baby].flash = 0;
  for (int j = 0; j < N_FISH; j++) box[j].friend_of = box[j].enemy_of = -2; // colours changed: refresh
  box[baby].ready = 0;
  born++;
}

static void report(const Event *e) {
  const char *a = look[e->a].name;
  const char *b = e->type == EV_EVOLVED ? SPECIES[e->b] : look[e->b].name;
  switch (e->type) {
    case EV_BIT:     log_add(a, " BIT ", b); break;
    case EV_ATE:     log_add(a, " ATE ", b); break;
    case EV_STARVED: log_add(a, " STARVED", 0); break;
    case EV_LIKES:   log_add(a, " NOW LIKES ", b); break;
    case EV_HATES:   log_add(a, " NOW HATES ", b); break;
    case EV_FOUGHT:  log_add(a, " FOUGHT ", b); break;
    case EV_EVOLVED: {
      log_add(a, e->b == K_ORCA ? " EVOLVED INTO AN " : " EVOLVED INTO A ", b);
      Ancestor *me = ancestor(tank.fish[e->a].id);
      if (me) me->kind = e->b;
      break;
    }
    case EV_BORN:
      dress_baby(e->a); // the dead animal's name is used up to here; now the baby gets its own
      {
        char parents[8]; // "ADA+BOB"
        for (int c = 0; c < 3; c++) { parents[c] = look[e->b].name[c]; parents[4 + c] = look[e->c].name[c]; }
        parents[3] = '+';
        parents[7] = 0;
        log_add(look[e->a].name, " WAS BORN TO ", parents);
      }
      break;
  }
}

// Copy the tank into the block of memory the website reads.
static void mirror_update(void) {
  fish_mirror.seq++;
  fish_view(&tank, born, fish_mirror.view);
  for (int i = 0; i < N_FISH; i++) {
    uint8_t *l = fish_mirror.looks[i];
    l[0] = tank.fish[i].g.r; l[1] = tank.fish[i].g.g; l[2] = tank.fish[i].g.b;
    l[3] = tank.fish[i].g.marking;
    l[4] = (uint8_t)look[i].name[0]; l[5] = (uint8_t)look[i].name[1]; l[6] = (uint8_t)look[i].name[2];
    l[7] = (uint8_t)art[i].flash;
  }
  fish_mirror.seq++;
  fish_mirror.seq_end = fish_mirror.seq;
}

// One tick of the tank: feeding, the rules, and whatever follows from them.
static void one_tick(int has_touch, uint32_t ticks, int *button_was, int *touch_was, uint32_t *last_feed, uint32_t *web_feeds) {
  // Feeding: the blue button drops food somewhere; a tap drops it right there;
  // and the website can ask for some too.
  int button = (int)(GPIOA_IDR & 1u);
  if (button && !*button_was) {
    tank.rng ^= SYST_CVR | 1u; // the moment you press makes the spot unpredictable
    tank_drop_food(&tank);
  }
  *button_was = button;

  int tx = 0, ty = 0;
  int touching = has_touch && touch_read(&tx, &ty) && ty < TANK_ROWS;
  if (touching && (!*touch_was || ticks - *last_feed >= 8)) { // hold to keep sprinkling
    tank_add_food(&tank, (float)tx, (float)ty);
    *last_feed = ticks;
  }
  *touch_was = touching;

  if (fish_mirror.feed_count != *web_feeds) {
    *web_feeds = fish_mirror.feed_count;
    float x = fish_mirror.feed_x, y = fish_mirror.feed_y;
    if (x >= 0.0f && x <= TANK_W && y >= 0.0f && y <= TANK_H) tank_add_food(&tank, x, y);
  }

  for (int i = 0; i < N_FISH; i++) { // remember where everyone was, for drawing in-between frames
    art[i].old_x = tank.fish[i].x; art[i].old_y = tank.fish[i].y;
    art[i].old_dx = tank.fish[i].dx; art[i].old_dy = tank.fish[i].dy;
  }
  tank_step(&tank);
  talk_notice(&tank); // before the dead are replaced, so their friends can mourn them
  for (int i = 0; i < N_FISH; i++) {
    tank.fish[i].score *= 0.9995f; // old successes slowly count for less
    if (tank.fish[i].dead) {
      Ancestor *gone = ancestor(tank.fish[i].id);
      if (gone) gone->dead = 1;
      tank_replace(&tank, i); // the best fish has a baby in its place
    }
  }
  for (int e = 0; e < tank.n_events; e++) report(&tank.events[e]);
  for (int i = 0; i < N_FISH; i++) after_tick(i);

  // Talking: one character a tick (each takes the model a few milliseconds), and any
  // finished line goes in the log as "ADA: ...".
  talk_notice(&tank);
  talk_work(&tank, 1);
  TalkLine line;
  while (talk_take(&line)) {
    log_add(look[line.speaker].name, ": ", line.text);
    tank_hear(&tank, line.speaker, line.listener, line.text); // words matter
    bubble_say(line.speaker, line.text, ticks + 120); // stays four seconds
  }
  bubbles_tick(ticks);

  // Day and night: when the light has changed enough, repaint the water (the animals are
  // drawn again straight after).
  static int light_level = 8;
  int level = (int)(tank.light * 8.0f + 0.5f);
  if (level != light_level) {
    light_level = level;
    set_water((float)level / 8.0f);
    Rect whole = {0, 0, LCD_W - 1, TANK_ROWS - 1};
    paint_background(whole);
    for (int k = 0; k < N_FOOD; k++) crumb[k].shown = 0;
  }
}

int main(void) {
  board_fast_clock();
  leds_init();
  led_red(1);
  lcd_init();
  int has_touch = touch_init();
  RCC_AHB1ENR |= 1u << 0; // port A, for the button

  fish_mirror.magic = 0x48534946u; // "FISH"
  fish_mirror.version = 8;

  tank_init(&tank, 0x1234ABCDu);
  talk_init(0x7A1C0FFEu);
  scenery_init();
  Rect whole_tank = {0, 0, LCD_W - 1, TANK_ROWS - 1};
  paint_background(whole_tank);
  draw_plants();
  lcd_fill_rect(0, TANK_ROWS, LCD_W, LCD_H - TANK_ROWS, BLACK);
  lcd_text(2, COUNT_Y, "FED:", 1, WHITE, BLACK);
  lcd_text(62, COUNT_Y, "BORN:", 1, WHITE, BLACK);
  lcd_text(128, COUNT_Y, "EATEN:", 1, WHITE, BLACK);
  log_add(has_touch ? "TAP THE WATER TO FEED" : "BLUE BUTTON DROPS FOOD", 0, 0);
  log_add("GREEN: HEALTH   ORANGE: HUNGER", 0, 0);
  log_add("+ BEST FRIEND   - WORST ENEMY", 0, 0);

  for (int i = 0; i < N_FISH; i++) {
    for (int w = 0; w < N_WEIGHTS; w++) tank.fish[i].brain.w[w] = FISH_BRAINS[i][w];
    give_name(i);
    tint_changed(i);
    remember(i);
    art[i].last_health = tank.fish[i].health;
    art[i].old_x = tank.fish[i].x; art[i].old_y = tank.fish[i].y;
    art[i].old_dx = tank.fish[i].dx; art[i].old_dy = tank.fish[i].dy;
    prepare(i, 1.0f);
    art[i].last = art[i].now;
  }
  lcd_text(196, COUNT_Y, "FPS", 1, WHITE, BLACK);
  led_red(0);

  // A free-running countdown, used to tell how much time has passed.
  SYST_RVR = 0xFFFFFF;
  SYST_CVR = 0;
  SYST_CSR = 5;
  const uint32_t tick_len = cpu_hz / 30; // the rules move forward 30 times a second...
  uint32_t clock_was = SYST_CVR, owed = tick_len, ticks = 0;
  uint32_t frames = 0, frames_at_last_count = 0; // ...and the screen redraws as often as it can

  uint32_t shown_fed = ~0u, shown_born = ~0u, shown_eaten = ~0u, last_feed = 0, web_feeds = 0;
  int button_was = 0, touch_was = 0;
  for (;;) {
    uint32_t clock_now = SYST_CVR;
    owed += (clock_was - clock_now) & 0xFFFFFFu;
    clock_was = clock_now;
    if (owed > 3 * tick_len) owed = 3 * tick_len; // fell behind: don't try to catch up for ever

    while (owed >= tick_len) {
      owed -= tick_len;
      one_tick(has_touch, ticks, &button_was, &touch_was, &last_feed, &web_feeds);
      ticks++;

      draw_plants();
      draw_box((int)(ticks % N_FISH)); // one fish's box per tick is plenty
      if (log_changed) log_draw();
      if (tank.total_eaten != shown_fed) { shown_fed = tank.total_eaten; show_number(26, shown_fed); }
      if (born != shown_born) { shown_born = born; show_number(92, shown_born); }
      if (tank.total_killed != shown_eaten) { shown_eaten = tank.total_killed; show_number(164, shown_eaten); }
      if (ticks % 30 == 0) { // once a second: how many pictures were drawn in that second
        char fps[3] = {(char)('0' + (frames - frames_at_last_count) / 10 % 10), (char)('0' + (frames - frames_at_last_count) % 10), 0};
        lcd_text(218, COUNT_Y, fps, 1, YELLOW, BLACK);
        frames_at_last_count = frames;
      }
      mirror_update();
      led_green((int)((ticks / 15) & 1u)); // heartbeat
    }

    // Draw every fish part-way between the last tick and this one.
    float blend = (float)owed / (float)tick_len;
    for (int i = 0; i < N_FISH; i++) prepare(i, blend);
    fish_mirror.motion_seq++;
    for (int i = 0; i < N_FISH; i++) {
      fish_mirror.motion[i][0] = art[i].x; fish_mirror.motion[i][1] = art[i].y;
      fish_mirror.motion[i][2] = art[i].dx; fish_mirror.motion[i][3] = art[i].dy;
    }
    fish_mirror.motion_seq++;
    fish_mirror.motion_seq_end = fish_mirror.motion_seq;
    for (int i = 0; i < N_FISH; i++) draw_fish(i);
    draw_food();
    frames++;
  }
}
