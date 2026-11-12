#include "fish_sim.h"

#define SIGHT      60.0f  // how far a fish notices other fish
#define FOOD_SIGHT 120.0f // how far it notices food
#define TOO_CLOSE  18.0f  // nearer than this to a neighbour feels crowded
#define WALL_ZONE  40.0f  // nearer than this to a wall, it senses the wall
#define EDGE       6.0f   // fish can't go closer to the glass than this
#define SWIM_FLOOR (TANK_H - FLOOR_H - 8.0f) // ...or lower than this, just above the seafloor
#define TURN_RATE  0.18f  // sharpest turn per tick
#define DRAIN      0.0005f // energy used per tick by an average-sized fish
#define MEAL       0.35f   // energy from one pellet

// Growing and evolving: eating earns meal points and adds size. Enough meal points and the
// fish evolves into the next species, which can grow bigger and hits harder.
#define BABY_SIZE   0.7f
#define GROW_PELLET 0.07f
#define GROW_BITE   0.08f
#define GROW_KILL   0.3f
static const float STAGE_NEEDS[N_STAGES] = {0.0f, 2.0f, 5.0f, 10.0f, 18.0f}; // meal points to reach each step
static const float STAGE_SIZE[N_STAGES]  = {1.0f, 1.45f, 1.95f, 2.5f, 3.1f}; // biggest size at each step (times its build gene)

// Hunting and fighting
#define PREY_RATIO  0.8f // a fish can hunt one whose power is below this fraction of its own
// ...but only when hungry enough (full fish don't hunt): see hunt_hunger(), set by its genes
#define FRIEND      0.3f // nobody attacks a fish it likes this much
// It fights an enemy whatever its size once its dislike passes a point set by its
// aggression trait: from -0.65 (peaceful) to -0.35 (aggressive). See fight_point().
#define BITE_DAMAGE 0.35f
#define BITE_REST   60   // ticks between bites
#define NEWBORN_SAFE 300 // ticks a newborn can't be attacked
#define LUNGE       1.4f  // a hunting fish with prey in sight swims this much faster...
#define LUNGE_COST  0.0005f // ...and burns this much extra energy per tick

// Plants and nutrients
#define PLANT_GROWTH 0.00022f // growth per tick, multiplied by (0.4 + nutrients)
#define PLANT_SEED   0.06f    // height a plant gives up to release one pellet of food
#define GRAZE        0.004f   // height nibbled (and energy gained) per tick by a small fish

static float root(float v) { return __builtin_sqrtf(v); }

// Squash any number into the range -1..1 (a cheap stand-in for tanh).
static float squash(float v) { return v / (1.0f + (v < 0.0f ? -v : v)); }

static float clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

uint32_t rng_next(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return *state = x;
}

float rng_unit(uint32_t *state) { return (float)(rng_next(state) >> 8) * (1.0f / 16777216.0f); }

float rng_normal(uint32_t *state) {
  float sum = rng_unit(state) + rng_unit(state) + rng_unit(state) + rng_unit(state);
  return (sum - 2.0f) * 1.732f;
}

static void note(Tank *t, int type, int a, int b) {
  if (t->n_events >= MAX_EVENTS) return; // a very busy tick: the extra ones go unreported
  Event *e = &t->events[t->n_events++];
  e->type = (uint8_t)type;
  e->a = (uint8_t)a;
  e->b = (uint8_t)b;
  e->c = (uint8_t)b;
}

// ---------- brains ----------

// Weight from sense `in` to hidden cell `hid`, and from hidden cell `hid` to decision `out`.
// Weight from sense `in` to first-layer cell `h`; from first-layer cell `h` to second-layer
// cell `m` (h = N_H1 is its resting level); from second-layer cell `m` to decision `o`
// (m = N_H2 is its resting level).
#define W_IN(brain, h, in) ((brain)->w[(h) * N_IN + (in)])
#define W_MID(brain, m, h) ((brain)->w[N_H1 * N_IN + (m) * (N_H1 + 1) + (h)])
#define W_OUT(brain, o, m) ((brain)->w[N_H1 * N_IN + N_H2 * (N_H1 + 1) + (o) * (N_H2 + 1) + (m)])

// The senses, in order. "ahead" and "right" are from the fish's own point of view.
enum {
  S_FOOD_AHEAD, S_FOOD_RIGHT,     // direction of the nearest food
  S_SCHOOL_AHEAD, S_SCHOOL_RIGHT, // where the middle of the nearby group is
  S_ALIGN_AHEAD, S_ALIGN_RIGHT,   // which way the nearby group is heading
  S_CROWD_AHEAD, S_CROWD_RIGHT,   // a neighbour that is too close
  S_WALL_AHEAD, S_WALL_RIGHT,     // the direction away from nearby walls
  S_HUNGER,                       // 0 = full, 1 = starving
  S_HURT,                         // 0 = healthy, 1 = nearly dead
  S_FRIEND_AHEAD, S_FRIEND_RIGHT, // where the fish it likes are
  S_ENEMY_AHEAD, S_ENEMY_RIGHT,   // where the fish it dislikes are
  S_PREY_AHEAD, S_PREY_RIGHT,     // nearest fish weak enough to hunt
  S_THREAT_AHEAD, S_THREAT_RIGHT, // nearest fish strong enough to hunt it
  S_FAMILY_AHEAD, S_FAMILY_RIGHT, // where its relatives (same family) are
  S_MY_SIZE,                      // how strong it is (size and species)
  S_BOLD, S_SOCIAL, S_AGGRO, S_LOYAL, S_CURIOUS, // its own personality, -1 .. 1
  S_ALWAYS_ON,                    // always 1 (lets a cell have a resting level)
};
enum { D_TURN, D_PUSH, D_BITE }; // the decisions

void brain_instinct(Brain *b) {
  for (int i = 0; i < N_WEIGHTS; i++) b->w[i] = 0.0f;

  // Hidden cell 0: "which way should I steer?"
  W_IN(b, 0, S_FOOD_RIGHT)   = 1.6f;  // towards food
  W_IN(b, 0, S_SCHOOL_RIGHT) = 0.8f;  // towards the group
  W_IN(b, 0, S_ALIGN_RIGHT)  = 0.6f;  // the same way as the group
  W_IN(b, 0, S_CROWD_RIGHT)  = -1.2f; // away from a fish that's too close
  W_IN(b, 0, S_WALL_RIGHT)   = 1.5f;  // away from walls
  W_IN(b, 0, S_FRIEND_RIGHT) = 0.6f;  // towards friends
  W_IN(b, 0, S_ENEMY_RIGHT)  = -0.8f; // away from enemies
  W_IN(b, 0, S_PREY_RIGHT)   = 0.5f;  // drift towards something edible
  W_IN(b, 0, S_THREAT_RIGHT) = -2.0f; // away from something that could eat me
  // Hidden cell 1: "I'm heading straight at a wall"
  W_IN(b, 1, S_WALL_AHEAD) = -2.0f;
  // Hidden cell 2: "worth hurrying" (food ahead, or hungry)
  W_IN(b, 2, S_FOOD_AHEAD) = 1.5f;
  W_IN(b, 2, S_HUNGER)     = 1.0f;
  // Hidden cell 3: "something dangerous is in front of me"
  W_IN(b, 3, S_THREAT_AHEAD) = 2.0f;
  // Hidden cell 4: "something dangerous is behind me"
  W_IN(b, 4, S_THREAT_AHEAD) = -1.5f;
  // Hidden cell 5: "hungry enough to bite"
  W_IN(b, 5, S_HUNGER)     = 2.0f;
  W_IN(b, 5, S_PREY_AHEAD) = 1.0f;
  W_IN(b, 5, S_ALWAYS_ON)  = -1.0f;
  // Hidden cell 6: "an enemy is right in front of me"
  W_IN(b, 6, S_ENEMY_AHEAD) = 2.0f;
  W_IN(b, 6, S_HURT)        = -2.0f; // ...but don't pick fights when already hurt
  W_IN(b, 6, S_AGGRO)       = 0.8f;  // ...unless that's its nature
  // Personality nudges the instincts too
  W_IN(b, 0, S_FAMILY_RIGHT) = 0.4f; // stay near the family
  W_IN(b, 5, S_BOLD)         = 0.8f; // bold animals bite sooner

  // The second layer starts by simply passing those seven cells on (the other cells of both
  // layers start blank, for training to put to use).
  for (int k = 0; k < 7; k++) W_MID(b, k, k) = 2.0f;

  W_OUT(b, D_TURN, 0) = 2.0f;      // turn = steer...
  W_OUT(b, D_TURN, 1) = 1.0f;      // ...swerve if facing a wall
  W_OUT(b, D_TURN, 3) = 1.0f;      // ...or facing a threat
  W_OUT(b, D_PUSH, 2) = 1.5f;      // swim harder when it's worth hurrying
  W_OUT(b, D_PUSH, 4) = 1.0f;      // or when being chased
  W_OUT(b, D_PUSH, N_H2) = -0.2f;  // otherwise cruise
  W_OUT(b, D_BITE, 5) = 2.0f;      // bite when hungry and prey is close
  W_OUT(b, D_BITE, 6) = 1.0f;      // or when an enemy is in my face
}

void brain_child(Brain *child, const Brain *parent, uint32_t *rng, float amount) {
  for (int i = 0; i < N_WEIGHTS; i++) child->w[i] = parent->w[i] + rng_normal(rng) * amount;
}

// ---------- setting up ----------

const uint8_t FOUNDER_COLOURS[N_FISH][3] = {
  {255, 140, 0},  {255, 90, 90},   {255, 210, 60},  {120, 230, 120}, {90, 200, 255},
  {200, 140, 255}, {255, 150, 200}, {240, 240, 240}, {160, 255, 220}, {180, 220, 90},
};

void fish_random_genes(Fish *f, uint32_t *rng) {
  f->g.pace = 0.85f + 0.4f * rng_unit(rng);
  f->g.slim = 0.8f + 0.45f * rng_unit(rng);
  f->g.bulk = 0.85f + 0.45f * rng_unit(rng);
  f->g.thrift = 0.8f + 0.4f * rng_unit(rng);
  f->g.boldness = rng_unit(rng);
  f->g.sociability = rng_unit(rng);
  f->g.aggression = rng_unit(rng);
  f->g.loyalty = rng_unit(rng);
  f->g.curiosity = rng_unit(rng);
}

static float fight_point(const Fish *f) { return -0.65f + 0.3f * f->g.aggression; }

// How hungry it has to be before it hunts: bold animals hunt sooner.
static float hunt_hunger(const Fish *f) { return 0.4f + 0.3f * f->g.boldness; }

float fish_power(const Fish *f) { return f->size * (1.0f + 0.2f * (float)f->stage); }
static int gentle(const Fish *f) { return f->kind == K_RAY || f->kind == K_WHALE; }
static int hunter(const Fish *f) { return f->kind == K_SHARK || f->kind == K_ORCA; }

// Bigger animals have more health and a harder bite. On top of that, the gentle giants
// (Ray, Whale) are tougher, and the hunters (Shark, Orca) bite harder still.
float fish_max_health(const Fish *f) { return (0.5f + 0.5f * f->size) * (gentle(f) ? 1.2f : 1.0f); }
float fish_bite(const Fish *f) { return f->size * (1.0f + 0.1f * (float)f->stage) * (hunter(f) ? 1.25f : 1.0f); }

// Which animal does it become at each step? The first three steps are the same for
// everyone; after that, hunters and gentle animals go different ways.
static uint8_t kind_for(int stage, int is_hunter) {
  switch (stage) {
    case 0: return K_FRY;
    case 1: return K_SQUID;
    case 2: return K_TURTLE;
    case 3: return is_hunter ? K_SHARK : K_RAY;
    default: return is_hunter ? K_ORCA : K_WHALE;
  }
}

static void place(Fish *f, uint32_t *rng) {
  f->x = 20.0f + rng_unit(rng) * (TANK_W - 40.0f);
  f->y = 20.0f + rng_unit(rng) * (SWIM_FLOOR - 40.0f);
  float dx = rng_unit(rng) * 2.0f - 1.0f, dy = rng_unit(rng) * 2.0f - 1.0f;
  float len = root(dx * dx + dy * dy);
  if (len < 0.05f) { dx = 1.0f; dy = 0.0f; len = 1.0f; }
  f->dx = dx / len;
  f->dy = dy / len;
  f->speed = 1.0f;
}

void tank_rebirth(Tank *t, int i) {
  Fish *f = &t->fish[i];
  place(f, &t->rng);
  f->energy = 0.6f;
  f->stage = 0;
  f->kind = K_FRY;
  f->growth = 0.0f;
  f->size = BABY_SIZE;
  f->health = fish_max_health(f);
  f->max_size = STAGE_SIZE[0] * f->g.bulk;
  f->age = 0;
  f->bite_wait = 0;
  f->dead = 0;
  f->eaten_by = NOBODY;
  for (int j = 0; j < N_FISH; j++) { t->like[i][j] = 0.0f; t->like[j][i] = 0.0f; t->said[i][j] = 0; t->said[j][i] = 0; }
}


// One gene from one of two parents, sometimes changed a little on the way.
static float inherit(Tank *t, float from_mum, float from_dad, float lo, float hi) {
  float v = (rng_next(&t->rng) & 1u) ? from_mum : from_dad;
  if (rng_unit(&t->rng) < 0.3f) v += rng_normal(&t->rng) * 0.08f * (hi - lo); // a mutation
  return clamp(v, lo, hi);
}

static uint8_t inherit_colour(Tank *t, uint8_t from_mum, uint8_t from_dad) {
  int v = (rng_next(&t->rng) & 1u) ? from_mum : from_dad;
  if (rng_unit(&t->rng) < 0.5f) v += (int)(rng_normal(&t->rng) * 18.0f);
  return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

// An animal too dark to see against the water gets its colour lifted (keeping its hue).
static void keep_visible(Genome *g) {
  int top = g->r > g->g ? (g->r > g->b ? g->r : g->b) : (g->g > g->b ? g->g : g->b);
  if (top >= 140) return;
  if (top < 1) { g->r = g->g = g->b = 140; return; }
  g->r = (uint8_t)(g->r * 140 / top);
  g->g = (uint8_t)(g->g * 140 / top);
  g->b = (uint8_t)(g->b * 140 / top);
}

int tank_replace(Tank *t, int gone) {
  // The mum: one of the three animals doing best, picked by lot (always the very best
  // would let one family take over the whole tank within minutes).
  int top[3] = {-1, -1, -1};
  for (int j = 0; j < N_FISH; j++) {
    if (j == gone || t->fish[j].dead) continue;
    for (int k = 0; k < 3; k++) {
      if (top[k] < 0 || t->fish[j].score > t->fish[top[k]].score) {
        for (int m = 2; m > k; m--) top[m] = top[m - 1];
        top[k] = j;
        break;
      }
    }
  }
  int choices = top[2] >= 0 ? 3 : top[1] >= 0 ? 2 : 1;
  int mum_i = top[0] < 0 ? (gone == 0 ? 1 : 0) : top[rng_next(&t->rng) % (uint32_t)choices];
  // The dad: the mum's best friend if she has a good one, otherwise the next best animal.
  int dad_i = -1;
  float fondest = 0.2f;
  for (int j = 0; j < N_FISH; j++)
    if (j != gone && j != mum_i && !t->fish[j].dead && t->like[mum_i][j] > fondest) { fondest = t->like[mum_i][j]; dad_i = j; }
  if (dad_i < 0)
    for (int j = 0; j < N_FISH; j++)
      if (j != gone && j != mum_i && !t->fish[j].dead && (dad_i < 0 || t->fish[j].score > t->fish[dad_i].score)) dad_i = j;
  if (dad_i < 0) dad_i = mum_i;
  const Fish *mum = &t->fish[mum_i], *dad = &t->fish[dad_i];

  tank_rebirth(t, gone);
  Fish *baby = &t->fish[gone];

  // The genome: each gene from one parent or the other, with the odd mutation.
  baby->g.pace = inherit(t, mum->g.pace, dad->g.pace, 0.85f, 1.25f);
  baby->g.slim = inherit(t, mum->g.slim, dad->g.slim, 0.8f, 1.25f);
  baby->g.bulk = inherit(t, mum->g.bulk, dad->g.bulk, 0.85f, 1.3f);
  baby->g.thrift = inherit(t, mum->g.thrift, dad->g.thrift, 0.8f, 1.2f);
  baby->g.boldness = inherit(t, mum->g.boldness, dad->g.boldness, 0.0f, 1.0f);
  baby->g.sociability = inherit(t, mum->g.sociability, dad->g.sociability, 0.0f, 1.0f);
  baby->g.aggression = inherit(t, mum->g.aggression, dad->g.aggression, 0.0f, 1.0f);
  baby->g.loyalty = inherit(t, mum->g.loyalty, dad->g.loyalty, 0.0f, 1.0f);
  baby->g.curiosity = inherit(t, mum->g.curiosity, dad->g.curiosity, 0.0f, 1.0f);
  baby->g.r = inherit_colour(t, mum->g.r, dad->g.r);
  baby->g.g = inherit_colour(t, mum->g.g, dad->g.g);
  baby->g.b = inherit_colour(t, mum->g.b, dad->g.b);
  keep_visible(&baby->g);
  baby->g.marking = rng_unit(&t->rng) < 0.1f ? (uint8_t)(rng_next(&t->rng) % 3u)
                  : (rng_next(&t->rng) & 1u) ? mum->g.marking : dad->g.marking;
  baby->max_size = STAGE_SIZE[0] * baby->g.bulk;

  // The brain: each cell, with all the connections coming into it, comes whole from one
  // parent (a block of genes that work together, like a chromosome). Then a few mutate.
  for (int h = 0; h < N_H1; h++) {
    const Brain *from = (rng_next(&t->rng) & 1u) ? &mum->brain : &dad->brain;
    for (int k = 0; k < N_IN; k++) W_IN(&baby->brain, h, k) = W_IN(from, h, k);
  }
  for (int m = 0; m < N_H2; m++) {
    const Brain *from = (rng_next(&t->rng) & 1u) ? &mum->brain : &dad->brain;
    for (int h = 0; h <= N_H1; h++) W_MID(&baby->brain, m, h) = W_MID(from, m, h);
  }
  for (int o = 0; o < N_OUT; o++) {
    const Brain *from = (rng_next(&t->rng) & 1u) ? &mum->brain : &dad->brain;
    for (int m = 0; m <= N_H2; m++) W_OUT(&baby->brain, o, m) = W_OUT(from, o, m);
  }
  for (int w = 0; w < N_WEIGHTS; w++)
    if (rng_unit(&t->rng) < 0.1f) baby->brain.w[w] += rng_normal(&t->rng) * 0.1f;

  // Its family
  baby->id = t->next_id++;
  if (t->next_id == 0) t->next_id = 1; // (after 65,535 births the numbers go round again)
  baby->mum = mum->id;
  baby->dad = dad->id;
  baby->family = mum->family;
  baby->gen = (uint16_t)((mum->gen > dad->gen ? mum->gen : dad->gen) + 1);

  baby->score = 0.0f;
  baby->eaten = baby->wall_hits = baby->bites = baby->kills = 0;
  for (int k = 0; k < 2; k++) {
    int parent = k ? dad_i : mum_i;
    t->like[gone][parent] = 0.5f;
    t->like[parent][gone] = 0.5f;
    t->said[gone][parent] = 1; // (no need to announce that a baby and its parents get on)
    t->said[parent][gone] = 1;
  }
  if (t->n_events < MAX_EVENTS) {
    Event *e = &t->events[t->n_events++];
    e->type = EV_BORN; e->a = (uint8_t)gone; e->b = (uint8_t)mum_i; e->c = (uint8_t)dad_i;
  }
  return mum_i;
}

int tank_add_food(Tank *t, float x, float y) {
  for (int k = 0; k < N_FOOD; k++) {
    if (!t->food[k].there) {
      t->food[k].x = clamp(x, 4.0f, TANK_W - 4.0f);
      t->food[k].y = clamp(y, 4.0f, SWIM_FLOOR);
      t->food[k].there = 1;
      return k;
    }
  }
  return -1;
}

int tank_drop_food(Tank *t) {
  float x = 12.0f + rng_unit(&t->rng) * (TANK_W - 24.0f);
  float y = 12.0f + rng_unit(&t->rng) * (SWIM_FLOOR - 24.0f);
  return tank_add_food(t, x, y);
}

void tank_init(Tank *t, uint32_t seed) {
  t->rng = seed | 1u;
  t->step = 0;
  t->total_eaten = t->total_killed = t->total_starved = 0;
  t->n_events = 0;
  t->nutrients = 0.3f;
  t->next_id = N_FISH + 1;
  for (int i = 0; i < N_FISH; i++) {
    Fish *f = &t->fish[i];
    fish_random_genes(f, &t->rng);
    f->g.r = FOUNDER_COLOURS[i][0]; f->g.g = FOUNDER_COLOURS[i][1]; f->g.b = FOUNDER_COLOURS[i][2];
    f->g.marking = (uint8_t)(i % 3);
    tank_rebirth(t, i);
    brain_instinct(&f->brain);
    f->id = (uint16_t)(i + 1); // the founders: each starts a family of its own
    f->mum = f->dad = 0;
    f->gen = 0;
    f->family = (uint8_t)i;
    // Everyone starts as a Fry, not all quite the same size.
    f->size = BABY_SIZE + 0.2f * rng_unit(&t->rng);
    f->health = fish_max_health(f);
    f->energy = 0.6f + 0.4f * rng_unit(&t->rng);
    f->age = NEWBORN_SAFE;
    f->score = 0.0f;
    f->eaten = f->wall_hits = f->bites = f->kills = 0;
  }
  for (int k = 0; k < N_FOOD; k++) t->food[k].there = 0;
  for (int p = 0; p < N_PLANTS; p++) {
    t->plants[p].x = 22.0f + (float)p * ((TANK_W - 44.0f) / (float)(N_PLANTS - 1)) + (rng_unit(&t->rng) - 0.5f) * 14.0f;
    t->plants[p].height = 0.3f + 0.5f * rng_unit(&t->rng);
  }
  tank_drop_food(t);
  tank_drop_food(t);
}

void tank_scatter_stages(Tank *t) {
  for (int i = 0; i < N_FISH; i++) {
    Fish *f = &t->fish[i];
    float luck = rng_unit(&t->rng);
    f->stage = luck < 0.35f ? 0 : luck < 0.65f ? 1 : luck < 0.85f ? 2 : 3;
    f->kind = kind_for(f->stage, rng_unit(&t->rng) < 0.5f);
    f->growth = STAGE_NEEDS[f->stage];
    f->max_size = STAGE_SIZE[f->stage] * f->g.bulk;
    f->size = f->max_size - 0.2f * rng_unit(&t->rng);
    f->health = fish_max_health(f);
  }
}

// ---------- sensing and thinking ----------

static float closeness(float distance) { // 1 when touching the wall, 0 outside the wall zone
  return distance < WALL_ZONE ? 1.0f - distance / WALL_ZONE : 0.0f;
}

static float plant_top(const Plant *p) { return TANK_H - FLOOR_H - p->height * PLANT_TALL; }

// May `a` attack `b` at all? Never a friend, never a newborn.
static int may_attack(const Tank *t, int a, int b) {
  const Fish *o = &t->fish[b];
  if (t->fish[a].g.loyalty > 0.5f && t->fish[a].family == o->family) return 0; // never its own family
  return !o->dead && o->age >= NEWBORN_SAFE && t->like[a][b] < FRIEND;
}

// Is `prey` weak enough for `hunter` to hunt?
static int is_prey(const Tank *t, int hunter, int prey) {
  return may_attack(t, hunter, prey) && fish_power(&t->fish[prey]) < fish_power(&t->fish[hunter]) * PREY_RATIO;
}

// Work out everything fish i can sense right now. Also reports things used elsewhere:
// how well it is swimming with the group, whether it is being crowded, whether it sees prey.
static void sense(const Tank *t, int i, float in[N_IN], float *with_group, int *crowded, int *sees_prey) {
  const Fish *me = &t->fish[i];
  const float ax = me->dx, ay = me->dy; // "ahead"
  const float rx = -ay, ry = ax;        // "to my right"
  for (int k = 0; k < N_IN; k++) in[k] = 0.0f;

  // Nearest food in sight: a pellet, or (for the two smallest species) a plant to nibble.
  float best = FOOD_SIGHT * FOOD_SIGHT, fx = 0.0f, fy = 0.0f;
  int found = 0;
  for (int k = 0; k < N_FOOD; k++) {
    if (!t->food[k].there) continue;
    float ox = t->food[k].x - me->x, oy = t->food[k].y - me->y;
    float d2 = ox * ox + oy * oy;
    if (d2 < best) { best = d2; fx = ox; fy = oy; found = 1; }
  }
  if (me->stage <= 1 || gentle(me)) {
    for (int p = 0; p < N_PLANTS; p++) {
      if (t->plants[p].height < 0.2f) continue;
      float ox = t->plants[p].x - me->x, oy = plant_top(&t->plants[p]) - me->y;
      float d2 = (ox * ox + oy * oy) * 1.5f; // a pellet is better than a plant the same distance away
      if (d2 < best) { best = d2; fx = ox; fy = oy; found = 1; }
    }
  }
  if (found) {
    float d = root(fx * fx + fy * fy) + 0.001f;
    float strength = (1.0f - 0.5f * d / FOOD_SIGHT) / d; // nearer food pulls harder
    in[S_FOOD_AHEAD] = (fx * ax + fy * ay) * strength;
    in[S_FOOD_RIGHT] = (fx * rx + fy * ry) * strength;
  }

  // The other fish
  const float my_power = fish_power(me);
  float cx = 0.0f, cy = 0.0f, hx = 0.0f, hy = 0.0f;
  float nearest = TOO_CLOSE * TOO_CLOSE, nx = 0.0f, ny = 0.0f;
  float friend_x = 0.0f, friend_y = 0.0f, enemy_x = 0.0f, enemy_y = 0.0f, kin_x = 0.0f, kin_y = 0.0f;
  float prey_d2 = SIGHT * SIGHT, prey_x = 0.0f, prey_y = 0.0f;
  float threat_d2 = SIGHT * SIGHT, threat_x = 0.0f, threat_y = 0.0f;
  int count = 0, pressed = 0, prey = 0, threat = 0;
  for (int j = 0; j < N_FISH; j++) {
    if (j == i || t->fish[j].dead) continue;
    const Fish *o = &t->fish[j];
    float ox = o->x - me->x, oy = o->y - me->y;
    float d2 = ox * ox + oy * oy;
    if (d2 < nearest) { nearest = d2; nx = ox; ny = oy; pressed = 1; }
    if (d2 >= SIGHT * SIGHT) continue;

    cx += ox; cy += oy; hx += o->dx; hy += o->dy; count++;

    float inv = 1.0f / (root(d2) + 0.001f);
    float feeling = t->like[i][j];
    if (feeling > 0.0f) { friend_x += ox * inv * feeling; friend_y += oy * inv * feeling; }
    else                { enemy_x -= ox * inv * feeling;  enemy_y -= oy * inv * feeling; }
    if (o->family == me->family) { kin_x += ox * inv; kin_y += oy * inv; }

    if (d2 < prey_d2 && is_prey(t, i, j)) { prey_d2 = d2; prey_x = ox; prey_y = oy; prey = 1; }
    if (d2 < threat_d2 && my_power < fish_power(o) * PREY_RATIO) { threat_d2 = d2; threat_x = ox; threat_y = oy; threat = 1; }
  }
  *with_group = 0.0f;
  if (count) {
    float inv = 1.0f / (float)count;
    cx *= inv / SIGHT; cy *= inv / SIGHT;
    hx *= inv; hy *= inv;
    in[S_SCHOOL_AHEAD] = cx * ax + cy * ay;
    in[S_SCHOOL_RIGHT] = cx * rx + cy * ry;
    in[S_ALIGN_AHEAD] = hx * ax + hy * ay;
    in[S_ALIGN_RIGHT] = hx * rx + hy * ry;
    if (in[S_ALIGN_AHEAD] > 0.0f) *with_group = in[S_ALIGN_AHEAD];
  }
  *crowded = 0;
  if (pressed) {
    float d = root(nearest) + 0.001f;
    float strength = (1.0f - d / TOO_CLOSE) / d;
    in[S_CROWD_AHEAD] = (nx * ax + ny * ay) * strength;
    in[S_CROWD_RIGHT] = (nx * rx + ny * ry) * strength;
    *crowded = d < 8.0f;
  }
  in[S_FRIEND_AHEAD] = friend_x * ax + friend_y * ay;
  in[S_FRIEND_RIGHT] = friend_x * rx + friend_y * ry;
  in[S_ENEMY_AHEAD] = enemy_x * ax + enemy_y * ay;
  in[S_ENEMY_RIGHT] = enemy_x * rx + enemy_y * ry;
  *sees_prey = prey;
  if (prey) {
    float d = root(prey_d2) + 0.001f;
    float strength = (1.0f - 0.7f * d / SIGHT) / d;
    in[S_PREY_AHEAD] = (prey_x * ax + prey_y * ay) * strength;
    in[S_PREY_RIGHT] = (prey_x * rx + prey_y * ry) * strength;
  }
  if (threat) {
    float d = root(threat_d2) + 0.001f;
    float strength = (1.0f - 0.7f * d / SIGHT) / d;
    in[S_THREAT_AHEAD] = (threat_x * ax + threat_y * ay) * strength;
    in[S_THREAT_RIGHT] = (threat_x * rx + threat_y * ry) * strength;
  }

  // Walls and the seafloor: an arrow pointing away from whichever are near
  float wx = closeness(me->x) - closeness(TANK_W - me->x);
  float wy = closeness(me->y) - closeness(SWIM_FLOOR + EDGE - me->y);
  in[S_WALL_AHEAD] = wx * ax + wy * ay;
  in[S_WALL_RIGHT] = wx * rx + wy * ry;

  in[S_FAMILY_AHEAD] = kin_x * ax + kin_y * ay;
  in[S_FAMILY_RIGHT] = kin_x * rx + kin_y * ry;
  in[S_MY_SIZE] = my_power / 5.0f;
  in[S_BOLD] = me->g.boldness * 2.0f - 1.0f;
  in[S_SOCIAL] = me->g.sociability * 2.0f - 1.0f;
  in[S_AGGRO] = me->g.aggression * 2.0f - 1.0f;
  in[S_LOYAL] = me->g.loyalty * 2.0f - 1.0f;
  in[S_CURIOUS] = me->g.curiosity * 2.0f - 1.0f;

  in[S_HUNGER] = 1.0f - me->energy;
  in[S_HURT] = 1.0f - me->health / fish_max_health(me);
  in[S_ALWAYS_ON] = 1.0f;
}

// Run the brain: senses in, decisions out (each between -1 and 1). What the hidden cells
// came to is handed back too, for anyone watching.
static void think(const Brain *b, const float in[N_IN], float h1[N_H1], float h2[N_H2], float out[N_OUT]) {
  for (int h = 0; h < N_H1; h++) {
    float sum = 0.0f;
    for (int k = 0; k < N_IN; k++) sum += W_IN(b, h, k) * in[k];
    h1[h] = squash(sum);
  }
  for (int m = 0; m < N_H2; m++) {
    float sum = W_MID(b, m, N_H1);
    for (int h = 0; h < N_H1; h++) sum += W_MID(b, m, h) * h1[h];
    h2[m] = squash(sum);
  }
  for (int o = 0; o < N_OUT; o++) {
    float sum = W_OUT(b, o, N_H2);
    for (int m = 0; m < N_H2; m++) sum += W_OUT(b, o, m) * h2[m];
    out[o] = squash(sum);
  }
}

// ---------- things that happen to fish ----------

static void feel(Tank *t, int who, int about, float change) {
  float before = t->like[who][about];
  float after = clamp(before + change, -1.0f, 1.0f);
  t->like[who][about] = after;
  // Report a friendship or a feud once, and not again until the feeling has faded away.
  uint8_t *said = &t->said[who][about];
  if (after >= 0.5f && *said != 1) { *said = 1; note(t, EV_LIKES, who, about); }
  else if (after <= -0.5f && *said != 2) { *said = 2; note(t, EV_HATES, who, about); }
  else if (after > -0.2f && after < 0.2f) *said = 0;
}

// Fish i has eaten something worth `points` meal points. Enough of them and it evolves.
static void fed(Tank *t, int i, float points, float size_gain) {
  Fish *f = &t->fish[i];
  f->growth += points * (2.0f - f->g.thrift); // thrifty animals get less growth from a meal
  while (f->stage < N_STAGES - 1 && f->growth >= STAGE_NEEDS[f->stage + 1]) {
    f->stage++;
    // Hunter or gentle? Decided at the third step by how it has lived so far, and kept after.
    int is_hunter = f->stage == 3 ? (f->bites + 2 * f->kills >= 2) : hunter(f);
    f->kind = kind_for(f->stage, is_hunter);
    f->max_size = STAGE_SIZE[f->stage] * f->g.bulk;
    f->health = fish_max_health(f); // evolving heals
    f->score += 5.0f;
    note(t, EV_EVOLVED, i, f->kind);
  }
  f->size = clamp(f->size + size_gain, 0.0f, f->max_size);
}

static void die(Tank *t, int i, int eaten_by) {
  Fish *f = &t->fish[i];
  f->dead = 1;
  f->health = 0.0f;
  f->eaten_by = (uint8_t)eaten_by;
  f->score -= 20.0f;
  // What's left of it feeds the plants: a whole body if it starved, scraps if it was eaten.
  if (eaten_by == NOBODY) { t->total_starved++; t->nutrients += 0.15f; note(t, EV_STARVED, i, i); }
  else { t->total_killed++; t->nutrients += 0.07f; note(t, EV_ATE, eaten_by, i); }
  if (t->nutrients > 1.0f) t->nutrients = 1.0f;
}

// The winner of a kill gets a big meal.
static void feast(Tank *t, int winner) {
  Fish *f = &t->fish[winner];
  f->energy = 1.0f;
  f->kills++;
  f->score += 20.0f; // a successful hunt pays well, or evolution breeds fish that never hunt
  fed(t, winner, 3.0f, GROW_KILL);
}

// Fish i wants to bite. Find something it may attack right in front of its mouth:
// prey if it is hungry, or an enemy whatever their sizes (which starts a fight).
static void try_bite(Tank *t, int i) {
  Fish *me = &t->fish[i];
  const float my_power = fish_power(me);
  int hungry = me->energy < hunt_hunger(me);
  int target = -1, target_is_prey = 0;
  float target_d2 = 1e9f;
  for (int j = 0; j < N_FISH; j++) {
    if (j == i || !may_attack(t, i, j)) continue;
    int prey = hungry && is_prey(t, i, j);
    int enemy = t->like[i][j] < fight_point(me);
    if (!prey && !enemy) continue;
    const Fish *o = &t->fish[j];
    float ox = o->x - me->x, oy = o->y - me->y;
    float reach = 6.0f * me->size + 4.0f * o->size;
    float d2 = ox * ox + oy * oy;
    if (d2 < reach * reach && ox * me->dx + oy * me->dy > 0.0f && d2 < target_d2) { target = j; target_d2 = d2; target_is_prey = prey; }
  }
  if (target < 0) return;

  Fish *other = &t->fish[target];
  float edge = clamp(my_power / fish_power(other), 0.5f, 1.6f); // how much stronger I am
  // The bigger the fish, the harder it bites. (And the bigger the fish being bitten,
  // the more health it has to lose.)
  const float my_bite = BITE_DAMAGE * fish_bite(me), its_bite = BITE_DAMAGE * fish_bite(other);
  me->bites++;
  me->bite_wait = BITE_REST;

  if (target_is_prey) {
    // Hunting: the prey is hurt, the hunter is fed.
    other->health -= my_bite;
    other->score -= 5.0f;
    me->energy = clamp(me->energy + 0.3f, 0.0f, 1.0f);
    me->score += 8.0f;
    fed(t, i, 1.0f, GROW_BITE);
    if (other->health > 0.0f) note(t, EV_BIT, i, target);
  } else {
    // A fight between enemies: both get hurt, the stronger one less.
    other->health -= my_bite * 0.7f;
    me->health -= its_bite * 0.5f; // it bites back
    other->bite_wait = BITE_REST;
    me->score += 2.0f * edge - 2.0f; // worth it only against someone weaker
    me->energy = clamp(me->energy + 0.15f, 0.0f, 1.0f); // even a bite in a fight is a mouthful
    fed(t, i, 0.5f, GROW_BITE * 0.5f);
    feel(t, i, target, -0.2f);
    if (other->health > 0.0f && me->health > 0.0f) note(t, EV_FOUGHT, i, target);
  }

  // Nobody forgets being attacked: the victim, anyone who saw it, and above all its friends.
  feel(t, target, i, -0.6f);
  for (int w = 0; w < N_FISH; w++) {
    if (w == i || w == target || t->fish[w].dead) continue;
    float ox = t->fish[w].x - other->x, oy = t->fish[w].y - other->y;
    if (ox * ox + oy * oy < SIGHT * SIGHT) {
      int close = t->like[w][target] > FRIEND || t->fish[w].family == other->family; // a friend or a relative
      feel(t, w, i, (close ? -0.35f : -0.15f) * (0.5f + t->fish[w].g.loyalty));
    }
  }

  if (other->health <= 0.0f) {
    die(t, target, i);
    feast(t, i);
    if (me->health <= 0.0f) me->health = 0.05f; // won, but only just
  } else if (me->health <= 0.0f) { // picked a fight and lost it
    die(t, i, target);
    feast(t, target);
  }
}

// Plants grow, release food when they are tall enough, and get nibbled by small fish.
static void tend_plants(Tank *t) {
  int pellets = 0;
  for (int k = 0; k < N_FOOD; k++) pellets += t->food[k].there;

  for (int p = 0; p < N_PLANTS; p++) {
    Plant *plant = &t->plants[p];
    plant->height += PLANT_GROWTH * (0.4f + t->nutrients);
    if (plant->height > 1.0f) plant->height = 1.0f;

    // Twice a second, a well-grown plant may let go of a pellet of food. Two food
    // slots are always kept free for whoever is feeding the tank by hand.
    if (t->step % 15 == (uint32_t)p && plant->height > 0.35f && pellets < N_FOOD - 2 &&
        rng_unit(&t->rng) < 0.3f * plant->height) {
      float x = plant->x + (rng_unit(&t->rng) - 0.5f) * 36.0f;
      float y = plant_top(plant) - 6.0f - rng_unit(&t->rng) * 150.0f; // food drifts well up into the water
      if (tank_add_food(t, x, y < 8.0f ? 8.0f : y) >= 0) {
        plant->height -= PLANT_SEED;
        t->nutrients -= 0.01f;
        pellets++;
      }
    }
  }

  // Grazing: the two smallest kinds, and the gentle giants, eat the plants directly, slowly.
  for (int i = 0; i < N_FISH; i++) {
    Fish *f = &t->fish[i];
    if (f->dead || (f->stage > 1 && !gentle(f)) || f->energy > 0.8f) continue;
    for (int p = 0; p < N_PLANTS; p++) {
      Plant *plant = &t->plants[p];
      float ox = plant->x - f->x, oy = plant_top(plant) - f->y;
      if (plant->height > 0.15f && ox > -9.0f && ox < 9.0f && oy > -10.0f && oy < 12.0f) {
        plant->height -= GRAZE;
        f->energy = clamp(f->energy + GRAZE, 0.0f, 1.0f);
        fed(t, i, 0.01f, 0.0005f);
        break;
      }
    }
  }

  t->nutrients *= 0.9996f; // the water slowly clears
  if (t->nutrients < 0.0f) t->nutrients = 0.0f;
}

void tank_step(Tank *t) {
  float turn[N_FISH], push[N_FISH], bite[N_FISH], with_group[N_FISH];
  int crowded[N_FISH], sees_prey[N_FISH];
  t->n_events = 0;

  // Everyone looks and decides first, then everyone moves, so no fish gets a head start.
  for (int i = 0; i < N_FISH; i++) {
    sense(t, i, t->seen[i], &with_group[i], &crowded[i], &sees_prey[i]);
    think(&t->fish[i].brain, t->seen[i], t->thought[i], t->thought2[i], t->decided[i]);
    turn[i] = t->decided[i][D_TURN];
    push[i] = t->decided[i][D_PUSH];
    bite[i] = t->decided[i][D_BITE];
  }

  for (int i = 0; i < N_FISH; i++) {
    Fish *f = &t->fish[i];

    // Turn: lean the heading towards the right (or left, if negative).
    float lean = turn[i] * TURN_RATE + rng_normal(&t->rng) * 0.05f * f->g.curiosity; // curious ones wander
    float nx = f->dx - f->dy * lean, ny = f->dy + f->dx * lean;
    float inv = 1.0f / root(nx * nx + ny * ny);
    f->dx = nx * inv;
    f->dy = ny * inv;

    // Swim: ease towards the speed the brain asked for. Good swimmers are faster;
    // big fish and hurt fish are a little slower. A hungry fish that has decided to
    // bite and can see prey lunges, which is what lets a big fish catch a small one.
    int hunting = bite[i] > 0.0f && f->energy < hunt_hunger(f) && sees_prey[i];
    float wanted = (0.6f + 0.6f * (push[i] + 1.0f)) * f->g.pace * (1.12f - 0.08f * f->size) * (0.7f + 0.3f * f->health / fish_max_health(f));
    if (hunting) wanted *= LUNGE;
    f->speed += (wanted - f->speed) * 0.1f;
    f->x += f->dx * f->speed;
    f->y += f->dy * f->speed;

    // The glass and the seafloor
    int hit = 0;
    if (f->x < EDGE)          { f->x = EDGE;          if (f->dx < 0.0f) f->dx = -f->dx; hit = 1; }
    if (f->x > TANK_W - EDGE) { f->x = TANK_W - EDGE; if (f->dx > 0.0f) f->dx = -f->dx; hit = 1; }
    if (f->y < EDGE)          { f->y = EDGE;          if (f->dy < 0.0f) f->dy = -f->dy; hit = 1; }
    if (f->y > SWIM_FLOOR)    { f->y = SWIM_FLOOR;    if (f->dy > 0.0f) f->dy = -f->dy; hit = 1; }

    // Hunger: bigger fish burn more.
    f->energy -= DRAIN * (0.6f + 0.4f * f->size) / f->g.thrift + (hunting ? LUNGE_COST : 0.0f);
    if (f->energy < 0.0f) f->energy = 0.0f;

    // Food pellets
    float reach = 6.0f + 3.0f * f->size;
    for (int k = 0; k < N_FOOD; k++) {
      if (!t->food[k].there) continue;
      float ox = t->food[k].x - f->x, oy = t->food[k].y - f->y;
      if (ox * ox + oy * oy < reach * reach) {
        t->food[k].there = 0;
        f->energy = clamp(f->energy + MEAL, 0.0f, 1.0f);
        f->eaten++;
        t->total_eaten++;
        f->score += 10.0f;
        fed(t, i, 1.0f, GROW_PELLET);
        // Anyone else who was right there, going for the same pellet, resents it.
        for (int j = 0; j < N_FISH; j++) {
          if (j == i || t->fish[j].dead) continue;
          float qx = t->food[k].x - t->fish[j].x, qy = t->food[k].y - t->fish[j].y;
          if (qx * qx + qy * qy < 30.0f * 30.0f) feel(t, j, i, -0.12f);
        }
      }
    }

    f->score += 0.004f * with_group[i] * (0.5f + f->g.sociability);
    if (crowded[i]) f->score -= 0.01f;
    if (hit) { f->score -= 0.05f; f->wall_hits++; }
    if (f->age < 60000) f->age++;
    if (f->bite_wait) f->bite_wait--;
  }

  // Biting: hunting and fighting
  for (int i = 0; i < N_FISH; i++) {
    const Fish *f = &t->fish[i];
    if (!f->dead && bite[i] > 0.0f && !f->bite_wait) try_bite(t, i);
  }

  // Health: starving wears a fish down; a well-fed fish heals.
  for (int i = 0; i < N_FISH; i++) {
    Fish *f = &t->fish[i];
    if (f->dead) continue;
    const float most = fish_max_health(f);
    if (f->energy <= 0.0f) {
      f->health -= 0.002f * most;
      if (f->health <= 0.0f) die(t, i, NOBODY);
    } else if (f->energy > 0.4f && f->health < most) {
      f->health = clamp(f->health + 0.001f * most, 0.0f, most);
    }
  }

  // Relationships grow out of time spent together: swimming side by side the same way
  // builds friendship, bumping into each other sours it.
  for (int i = 0; i < N_FISH; i++) {
    if (t->fish[i].dead) continue;
    for (int j = i + 1; j < N_FISH; j++) {
      if (t->fish[j].dead) continue;
      const Fish *a = &t->fish[i], *b = &t->fish[j];
      float ox = b->x - a->x, oy = b->y - a->y;
      float d2 = ox * ox + oy * oy;
      if (d2 >= SIGHT * SIGHT) continue;
      if (d2 < 8.0f * 8.0f) { feel(t, i, j, -0.004f); feel(t, j, i, -0.004f); }
      else if (a->dx * b->dx + a->dy * b->dy > 0.7f) { // sociable animals warm to company faster
        feel(t, i, j, 0.0015f * (0.4f + 1.2f * a->g.sociability));
        feel(t, j, i, 0.0015f * (0.4f + 1.2f * b->g.sociability));
      }
    }
  }
  if (t->step % 16 == 0) { // feelings fade slowly if nothing keeps them going
    for (int i = 0; i < N_FISH; i++)
      for (int j = 0; j < N_FISH; j++) t->like[i][j] *= 0.997f;
  }

  tend_plants(t);
  t->step++;
}
