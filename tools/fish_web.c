// The bridge between the fish rules (Fish/fish_sim.c) and a web page.
//
// GitHub compiles this, together with fish_sim.c and the freshly trained brains, into
// fish.wasm: a file web browsers can run. The status website loads it, so the tank shown
// there follows exactly the same rules, with the same brains, as the tank on the board.
//
// The page calls web_step() 30 times a second and reads the numbers back from web_state().

#include "fish_sim.h"
#include "fish_brains.h"

#define EXPORT __attribute__((visibility("default")))

// There is no standard library here either, so provide the two helpers compilers ask for.
void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  while (n--) *d++ = *s++;
  return dst;
}
void *memset(void *dst, int value, __SIZE_TYPE__ n) {
  unsigned char *d = dst;
  while (n--) *d++ = (unsigned char)value;
  return dst;
}

#define PER_FISH 12
#define HEADER 8
#define STATE_SIZE (HEADER + N_FISH * PER_FISH + N_FOOD * 3 + N_FISH * N_FISH)

static Tank tank;
static float state[STATE_SIZE];
static int events[1 + N_FISH * 3]; // how many fish were replaced this tick, then (who, parent, eaten by) each
static unsigned born;

static void publish(void) {
  float *p = state;
  *p++ = (float)tank.step;
  *p++ = (float)tank.total_eaten;
  *p++ = (float)tank.total_killed;
  *p++ = (float)tank.total_starved;
  *p++ = (float)born;
  *p++ = 0; *p++ = 0; *p++ = 0;
  for (int i = 0; i < N_FISH; i++) {
    const Fish *f = &tank.fish[i];
    *p++ = f->x; *p++ = f->y; *p++ = f->dx; *p++ = f->dy;
    *p++ = f->size; *p++ = f->slim; *p++ = f->energy; *p++ = f->health;
    *p++ = f->score; *p++ = (float)f->age; *p++ = (float)f->kills; *p++ = (float)f->eaten;
  }
  for (int k = 0; k < N_FOOD; k++) {
    *p++ = tank.food[k].x; *p++ = tank.food[k].y; *p++ = (float)tank.food[k].there;
  }
  for (int i = 0; i < N_FISH; i++)
    for (int j = 0; j < N_FISH; j++) *p++ = tank.like[i][j];
}

EXPORT void web_init(unsigned seed) {
  tank_init(&tank, seed);
  for (int i = 0; i < N_FISH; i++)
    for (int w = 0; w < N_WEIGHTS; w++) tank.fish[i].brain.w[w] = FISH_BRAINS[i][w];
  born = 0;
  events[0] = 0;
  publish();
}

EXPORT void web_step(void) {
  tank_step(&tank);
  int n = 0;
  for (int i = 0; i < N_FISH; i++) {
    tank.fish[i].score *= 0.9995f; // old successes slowly count for less (as on the board)
    if (tank.fish[i].dead) {
      int eaten_by = tank.fish[i].eaten_by;
      int parent = tank_replace(&tank, i);
      events[1 + n * 3] = i;
      events[2 + n * 3] = parent;
      events[3 + n * 3] = eaten_by;
      n++;
      born++;
    }
  }
  events[0] = n;
  publish();
}

EXPORT void web_feed(float x, float y) { tank_add_food(&tank, x, y); }
EXPORT void web_feed_anywhere(void) { tank_drop_food(&tank); }

EXPORT float *web_state(void) { return state; }
EXPORT int *web_events(void) { return events; }
EXPORT int web_n_fish(void) { return N_FISH; }
EXPORT int web_n_food(void) { return N_FOOD; }
EXPORT int web_per_fish(void) { return PER_FISH; }
EXPORT int web_header(void) { return HEADER; }
EXPORT int web_generations(void) { return FISH_BRAINS_GENERATIONS; }
