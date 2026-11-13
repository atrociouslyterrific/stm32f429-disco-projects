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
#include "fish_talk.h"
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
static int events[1 + MAX_EVENTS * 4]; // how many things happened this tick, then (type, a, b, c) for each
static unsigned born;

static void publish(void) {
  fish_view(&tank, born, state);
  events[0] = tank.n_events;
  for (int e = 0; e < tank.n_events; e++) {
    events[1 + e * 4] = tank.events[e].type;
    events[2 + e * 4] = tank.events[e].a;
    events[3 + e * 4] = tank.events[e].b;
    events[4 + e * 4] = tank.events[e].c;
  }
}

EXPORT void web_init(unsigned seed) {
  tank_init(&tank, seed);
  for (int i = 0; i < N_FISH; i++)
    for (int w = 0; w < N_WEIGHTS; w++) tank.fish[i].brain.w[w] = FISH_BRAINS[i][w];
  born = 0;
  talk_init(seed ^ 0x5EA5EA5Eu);
  publish();
}

EXPORT void web_step(void) {
  tank_step(&tank);
  talk_notice(&tank); // before the dead are replaced, so their friends can mourn them
  for (int i = 0; i < N_FISH; i++) {
    tank.fish[i].score *= 0.9995f; // old successes slowly count for less (as on the board)
    if (tank.fish[i].dead) {       // the best fish has a baby in its place
      tank_replace(&tank, i);
      born++;
    }
  }
  talk_notice(&tank); // ...and again for the births
  talk_work(&tank, 24); // a few characters each tick, like the board (only quicker)
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

// A look inside the brains (see "seen", "thought" and "decided" in fish_sim.h).
// A brain's weights are laid out as in fish_sim.c (W_IN, W_MID, W_OUT): first-layer cells,
// a weight per sense each; second-layer cells, a weight per first-layer cell plus a resting
// level each; decisions, a weight per second-layer cell plus a resting level each.
EXPORT float *web_brain(int i) { return tank.fish[i].brain.w; }
EXPORT float *web_seen(void) { return &tank.seen[0][0]; }
EXPORT float *web_thought(void) { return &tank.thought[0][0]; }   // first hidden layer
EXPORT float *web_thought2(void) { return &tank.thought2[0][0]; } // second hidden layer
EXPORT float *web_decided(void) { return &tank.decided[0][0]; }
EXPORT int web_n_in(void) { return N_IN; }
EXPORT int web_n_h1(void) { return N_H1; }
EXPORT int web_n_h2(void) { return N_H2; }
EXPORT int web_n_weights(void) { return N_WEIGHTS; }
EXPORT int web_n_out(void) { return N_OUT; }

// Talking (see Fish/fish_talk.h). The page sets each animal's name, collects finished lines
// with web_talk_take(), and reads who is speaking right now with web_talk_now().
static TalkLine taken;
static char so_far[TALK_LINE + 1];
static int now_speaker, now_listener;
EXPORT int web_talk_available(void) { return talk_available(); }
EXPORT void web_set_name(int i, int a, int b, int c) { char n[3] = {(char)a, (char)b, (char)c}; talk_set_name(i, n); }
EXPORT void web_talk_quiet(int on) { talk_quiet(on); }
static int heard;
// A finished line; the listener hears it (words matter: see tank_hear in fish_sim.h).
EXPORT int web_talk_take(void) {
  if (!talk_take(&taken)) return 0;
  heard = tank_hear(&tank, taken.speaker, taken.listener, taken.text);
  return 1;
}
EXPORT int web_talk_effect(void) { return heard; } // what hearing it did (HEARD_...)
EXPORT int web_talk_speaker(void) { return taken.speaker; }
EXPORT int web_talk_listener(void) { return taken.listener; }
EXPORT char *web_talk_text(void) { return taken.text; }
EXPORT int web_talk_now(void) { return talk_current(&now_speaker, &now_listener, so_far); }
EXPORT int web_talk_now_speaker(void) { return now_speaker; }
EXPORT char *web_talk_now_text(void) { return so_far; }

// The rocks (centre x, centre y, radius each), and the environment.
EXPORT const float *web_obstacles(void) { return &OBSTACLES[0][0]; }
EXPORT int web_n_obstacles(void) { return N_OBSTACLES; }
EXPORT float web_light(void) { return tank.light; }
EXPORT float web_current_x(void) { return tank.current_x; }
