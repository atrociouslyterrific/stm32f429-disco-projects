// The bridge between the fish rules (Fish/fish_sim.c) and a web page.
//
// GitHub compiles this, together with the rules and the freshly trained brains, into
// fish.wasm: a file web browsers can run. The website loads it, so the tank shown there
// follows exactly the same rules, with the same brains, as the tank on the board.
//
// The page calls web_step() 30 times a second and reads the numbers back from web_state()
// (laid out as in Fish/fish_view.h) and what happened from web_events().

#include "fish_sim.h"
#include "fish_view.h"
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

static Tank tank;
static float state[VIEW_SIZE];
static int events[1 + MAX_EVENTS * 3]; // how many things happened this tick, then (type, a, b) for each
static unsigned born;

static void publish(void) {
  fish_view(&tank, born, state);
  events[0] = tank.n_events;
  for (int e = 0; e < tank.n_events; e++) {
    events[1 + e * 3] = tank.events[e].type;
    events[2 + e * 3] = tank.events[e].a;
    events[3 + e * 3] = tank.events[e].b;
  }
}

EXPORT void web_init(unsigned seed) {
  tank_init(&tank, seed);
  for (int i = 0; i < N_FISH; i++)
    for (int w = 0; w < N_WEIGHTS; w++) tank.fish[i].brain.w[w] = FISH_BRAINS[i][w];
  born = 0;
  publish();
}

EXPORT void web_step(void) {
  tank_step(&tank);
  for (int i = 0; i < N_FISH; i++) {
    tank.fish[i].score *= 0.9995f; // old successes slowly count for less (as on the board)
    if (tank.fish[i].dead) {       // the best fish has a baby in its place
      tank_replace(&tank, i);
      born++;
    }
  }
  publish();
}

EXPORT void web_feed(float x, float y) { tank_add_food(&tank, x, y); }
EXPORT void web_feed_anywhere(void) { tank_drop_food(&tank); }

EXPORT float *web_state(void) { return state; }
EXPORT int *web_events(void) { return events; }
EXPORT int web_max_events(void) { return MAX_EVENTS; }
EXPORT int web_generations(void) { return FISH_BRAINS_GENERATIONS; }

// The layout of web_state(), so the page never has to guess it.
EXPORT int web_n_fish(void) { return N_FISH; }
EXPORT int web_n_food(void) { return N_FOOD; }
EXPORT int web_n_plants(void) { return N_PLANTS; }
EXPORT int web_per_fish(void) { return VIEW_PER_FISH; }
EXPORT int web_fish_at(void) { return VIEW_FISH_AT; }
EXPORT int web_food_at(void) { return VIEW_FOOD_AT; }
EXPORT int web_like_at(void) { return VIEW_LIKE_AT; }
EXPORT int web_plant_at(void) { return VIEW_PLANT_AT; }
EXPORT int web_view_size(void) { return VIEW_SIZE; }
