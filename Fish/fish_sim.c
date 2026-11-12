#include "fish_sim.h"

#define SIGHT      60.0f  // how far a fish notices other fish
#define FOOD_SIGHT 120.0f // how far it notices food
#define TOO_CLOSE  18.0f  // nearer than this to a neighbour feels crowded
#define WALL_ZONE  40.0f  // nearer than this to a wall, it senses the wall
#define EDGE       6.0f   // fish can't go closer to the glass than this
#define EAT_REACH  8.0f
#define TURN_RATE  0.18f  // sharpest turn per tick
#define DRAIN      0.0006f // energy used per tick
#define MEAL       0.35f   // energy from one pellet

static float root(float v) { return __builtin_sqrtf(v); }

// Squash any number into the range -1..1 (a cheap stand-in for tanh).
static float squash(float v) { return v / (1.0f + (v < 0.0f ? -v : v)); }

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

// Weight from sense `in` to hidden cell `hid`, and from hidden cell `hid` to decision `out`.
#define W_IN(brain, hid, in)   ((brain)->w[(hid) * N_IN + (in)])
#define W_OUT(brain, out, hid) ((brain)->w[N_HID * N_IN + (out) * (N_HID + 1) + (hid)])

// The senses, in order. "ahead" and "right" are from the fish's own point of view.
enum {
  S_FOOD_AHEAD, S_FOOD_RIGHT,     // direction of the nearest food
  S_SCHOOL_AHEAD, S_SCHOOL_RIGHT, // where the middle of the nearby group is
  S_ALIGN_AHEAD, S_ALIGN_RIGHT,   // which way the nearby group is heading
  S_CROWD_AHEAD, S_CROWD_RIGHT,   // a neighbour that is too close
  S_WALL_AHEAD, S_WALL_RIGHT,     // the direction away from nearby walls
  S_HUNGER,                       // 0 = full, 1 = starving
  S_ALWAYS_ON,                    // always 1 (lets a cell have a resting level)
};

void brain_instinct(Brain *b) {
  for (int i = 0; i < N_WEIGHTS; i++) b->w[i] = 0.0f;

  // Hidden cell 0: "which way should I steer?"
  W_IN(b, 0, S_FOOD_RIGHT)   = 1.6f;  // towards food
  W_IN(b, 0, S_SCHOOL_RIGHT) = 0.8f;  // towards the group
  W_IN(b, 0, S_ALIGN_RIGHT)  = 0.6f;  // the same way as the group
  W_IN(b, 0, S_CROWD_RIGHT)  = -1.2f; // away from a fish that's too close
  W_IN(b, 0, S_WALL_RIGHT)   = 1.5f;  // away from walls
  // Hidden cell 1: "I'm heading straight at a wall"
  W_IN(b, 1, S_WALL_AHEAD) = -2.0f;
  // Hidden cell 2: "worth hurrying"
  W_IN(b, 2, S_FOOD_AHEAD) = 1.5f;
  W_IN(b, 2, S_HUNGER)     = 1.0f;

  W_OUT(b, 0, 0) = 2.0f;      // turn = steer...
  W_OUT(b, 0, 1) = 1.0f;      // ...plus swerve if facing a wall
  W_OUT(b, 1, 2) = 1.5f;      // swim harder when it's worth hurrying
  W_OUT(b, 1, N_HID) = -0.2f; // otherwise cruise
}

void brain_child(Brain *child, const Brain *parent, uint32_t *rng, float amount) {
  for (int i = 0; i < N_WEIGHTS; i++) child->w[i] = parent->w[i] + rng_normal(rng) * amount;
}

void fish_place(Fish *f, uint32_t *rng) {
  f->x = 20.0f + rng_unit(rng) * (TANK_W - 40.0f);
  f->y = 20.0f + rng_unit(rng) * (TANK_H - 40.0f);
  float dx = rng_unit(rng) * 2.0f - 1.0f, dy = rng_unit(rng) * 2.0f - 1.0f;
  float len = root(dx * dx + dy * dy);
  if (len < 0.05f) { dx = 1.0f; dy = 0.0f; len = 1.0f; }
  f->dx = dx / len;
  f->dy = dy / len;
  f->speed = 1.0f;
  f->energy = 0.7f + 0.3f * rng_unit(rng);
  f->score = 0.0f;
  f->eaten = 0;
  f->wall_hits = 0;
}

int tank_drop_food(Tank *t) {
  for (int k = 0; k < N_FOOD; k++) {
    if (!t->food[k].there) {
      t->food[k].x = 12.0f + rng_unit(&t->rng) * (TANK_W - 24.0f);
      t->food[k].y = 12.0f + rng_unit(&t->rng) * (TANK_H - 24.0f);
      t->food[k].there = 1;
      return k;
    }
  }
  return -1;
}

void tank_init(Tank *t, uint32_t seed) {
  t->rng = seed | 1u;
  t->step = 0;
  t->total_eaten = 0;
  for (int i = 0; i < N_FISH; i++) {
    fish_place(&t->fish[i], &t->rng);
    brain_instinct(&t->fish[i].brain);
  }
  for (int k = 0; k < N_FOOD; k++) t->food[k].there = 0;
  tank_drop_food(t);
  tank_drop_food(t);
}

static float closeness(float distance) { // 1 when touching the wall, 0 outside the wall zone
  return distance < WALL_ZONE ? 1.0f - distance / WALL_ZONE : 0.0f;
}

// Work out everything fish i can sense right now. Also reports two things used for
// scoring: how well it is swimming with the group, and whether it is being crowded.
static void sense(const Tank *t, int i, float in[N_IN], float *with_group, int *crowded) {
  const Fish *me = &t->fish[i];
  const float ax = me->dx, ay = me->dy; // "ahead"
  const float rx = -ay, ry = ax;        // "to my right"
  for (int k = 0; k < N_IN; k++) in[k] = 0.0f;

  // Nearest food in sight
  float best = FOOD_SIGHT * FOOD_SIGHT, fx = 0.0f, fy = 0.0f;
  int found = 0;
  for (int k = 0; k < N_FOOD; k++) {
    if (!t->food[k].there) continue;
    float ox = t->food[k].x - me->x, oy = t->food[k].y - me->y;
    float d2 = ox * ox + oy * oy;
    if (d2 < best) { best = d2; fx = ox; fy = oy; found = 1; }
  }
  if (found) {
    float d = root(best) + 0.001f;
    float strength = (1.0f - 0.5f * d / FOOD_SIGHT) / d; // nearer food pulls harder
    in[S_FOOD_AHEAD] = (fx * ax + fy * ay) * strength;
    in[S_FOOD_RIGHT] = (fx * rx + fy * ry) * strength;
  }

  // The other fish
  float cx = 0.0f, cy = 0.0f, hx = 0.0f, hy = 0.0f;
  float nearest = TOO_CLOSE * TOO_CLOSE, nx = 0.0f, ny = 0.0f;
  int count = 0, pressed = 0;
  for (int j = 0; j < N_FISH; j++) {
    if (j == i) continue;
    const Fish *o = &t->fish[j];
    float ox = o->x - me->x, oy = o->y - me->y;
    float d2 = ox * ox + oy * oy;
    if (d2 < SIGHT * SIGHT) { cx += ox; cy += oy; hx += o->dx; hy += o->dy; count++; }
    if (d2 < nearest) { nearest = d2; nx = ox; ny = oy; pressed = 1; }
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

  // Walls: an arrow pointing away from whichever walls are near
  float wx = closeness(me->x) - closeness(TANK_W - me->x);
  float wy = closeness(me->y) - closeness(TANK_H - me->y);
  in[S_WALL_AHEAD] = wx * ax + wy * ay;
  in[S_WALL_RIGHT] = wx * rx + wy * ry;

  in[S_HUNGER] = 1.0f - me->energy;
  in[S_ALWAYS_ON] = 1.0f;
}

// Run the brain: senses in, two decisions out (each between -1 and 1).
static void think(const Brain *b, const float in[N_IN], float out[N_OUT]) {
  float hidden[N_HID];
  for (int h = 0; h < N_HID; h++) {
    float sum = 0.0f;
    for (int k = 0; k < N_IN; k++) sum += W_IN(b, h, k) * in[k];
    hidden[h] = squash(sum);
  }
  for (int o = 0; o < N_OUT; o++) {
    float sum = W_OUT(b, o, N_HID);
    for (int h = 0; h < N_HID; h++) sum += W_OUT(b, o, h) * hidden[h];
    out[o] = squash(sum);
  }
}

void tank_step(Tank *t) {
  float turn[N_FISH], push[N_FISH], with_group[N_FISH];
  int crowded[N_FISH];

  // Everyone looks and decides first, then everyone moves, so no fish gets a head start.
  for (int i = 0; i < N_FISH; i++) {
    float in[N_IN], out[N_OUT];
    sense(t, i, in, &with_group[i], &crowded[i]);
    think(&t->fish[i].brain, in, out);
    turn[i] = out[0];
    push[i] = out[1];
  }

  for (int i = 0; i < N_FISH; i++) {
    Fish *f = &t->fish[i];

    // Turn: lean the heading towards the right (or left, if negative).
    float lean = turn[i] * TURN_RATE;
    float nx = f->dx - f->dy * lean, ny = f->dy + f->dx * lean;
    float inv = 1.0f / root(nx * nx + ny * ny);
    f->dx = nx * inv;
    f->dy = ny * inv;

    // Swim: ease towards the speed the brain asked for.
    float wanted = 0.6f + 0.6f * (push[i] + 1.0f);
    f->speed += (wanted - f->speed) * 0.1f;
    f->x += f->dx * f->speed;
    f->y += f->dy * f->speed;

    // The glass
    int hit = 0;
    if (f->x < EDGE)          { f->x = EDGE;          if (f->dx < 0.0f) f->dx = -f->dx; hit = 1; }
    if (f->x > TANK_W - EDGE) { f->x = TANK_W - EDGE; if (f->dx > 0.0f) f->dx = -f->dx; hit = 1; }
    if (f->y < EDGE)          { f->y = EDGE;          if (f->dy < 0.0f) f->dy = -f->dy; hit = 1; }
    if (f->y > TANK_H - EDGE) { f->y = TANK_H - EDGE; if (f->dy > 0.0f) f->dy = -f->dy; hit = 1; }

    f->energy -= DRAIN;
    if (f->energy < 0.0f) f->energy = 0.0f;

    for (int k = 0; k < N_FOOD; k++) {
      if (!t->food[k].there) continue;
      float ox = t->food[k].x - f->x, oy = t->food[k].y - f->y;
      if (ox * ox + oy * oy < EAT_REACH * EAT_REACH) {
        t->food[k].there = 0;
        f->energy += MEAL;
        if (f->energy > 1.0f) f->energy = 1.0f;
        f->eaten++;
        t->total_eaten++;
        f->score += 10.0f;
      }
    }

    f->score += 0.004f * with_group[i];
    if (crowded[i]) f->score -= 0.01f;
    if (hit) { f->score -= 0.05f; f->wall_hits++; }
  }

  // New food appears now and then, as long as there isn't much about.
  if (t->step % 45 == 0) {
    int about = 0;
    for (int k = 0; k < N_FOOD; k++) about += t->food[k].there;
    if (about < 3) tank_drop_food(t);
  }
  t->step++;
}
