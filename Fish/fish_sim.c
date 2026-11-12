#include "fish_sim.h"

#define SIGHT      60.0f  // how far a fish notices other fish
#define FOOD_SIGHT 120.0f // how far it notices food
#define TOO_CLOSE  18.0f  // nearer than this to a neighbour feels crowded
#define WALL_ZONE  40.0f  // nearer than this to a wall, it senses the wall
#define EDGE       6.0f   // fish can't go closer to the glass than this
#define TURN_RATE  0.18f  // sharpest turn per tick
#define DRAIN      0.0005f // energy used per tick by an average-sized fish
#define MEAL       0.35f   // energy from one pellet

// Eating each other
#define PREY_RATIO  0.8f // a fish can only bite one smaller than this fraction of itself
#define BITE_HUNGER 0.6f // ...and only when its energy is below this (full fish don't hunt)
#define FRIEND      0.3f // ...and never a fish it likes this much
#define BITE_DAMAGE 0.5f
#define LUNGE       1.4f  // a hunting fish with prey in sight swims this much faster...
#define LUNGE_COST  0.0005f // ...and burns this much extra energy per tick
#define BITE_REST   45   // ticks between bites
#define NEWBORN_SAFE 150 // ticks a newborn can't be bitten

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
  S_HURT,                         // 0 = healthy, 1 = nearly dead
  S_FRIEND_AHEAD, S_FRIEND_RIGHT, // where the fish it likes are
  S_ENEMY_AHEAD, S_ENEMY_RIGHT,   // where the fish it dislikes are
  S_PREY_AHEAD, S_PREY_RIGHT,     // nearest fish small enough to eat
  S_THREAT_AHEAD, S_THREAT_RIGHT, // nearest fish big enough to eat it
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

  W_OUT(b, D_TURN, 0) = 2.0f;      // turn = steer...
  W_OUT(b, D_TURN, 1) = 1.0f;      // ...swerve if facing a wall
  W_OUT(b, D_TURN, 3) = 1.0f;      // ...or facing a threat
  W_OUT(b, D_PUSH, 2) = 1.5f;      // swim harder when it's worth hurrying
  W_OUT(b, D_PUSH, 4) = 1.0f;      // or when being chased
  W_OUT(b, D_PUSH, N_HID) = -0.2f; // otherwise cruise
  W_OUT(b, D_BITE, 5) = 2.0f;
}

void brain_child(Brain *child, const Brain *parent, uint32_t *rng, float amount) {
  for (int i = 0; i < N_WEIGHTS; i++) child->w[i] = parent->w[i] + rng_normal(rng) * amount;
}

void fish_random_genes(Fish *f, uint32_t *rng) {
  f->max_size = 0.9f + 0.8f * rng_unit(rng);  // 0.9 .. 1.7
  f->pace     = 0.85f + 0.4f * rng_unit(rng); // 0.85 .. 1.25
  f->slim     = 0.8f + 0.45f * rng_unit(rng); // 0.8 .. 1.25
}

static void place(Fish *f, uint32_t *rng) {
  f->x = 20.0f + rng_unit(rng) * (TANK_W - 40.0f);
  f->y = 20.0f + rng_unit(rng) * (TANK_H - 40.0f);
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
  f->health = 1.0f;
  f->size = 0.7f;
  f->age = 0;
  f->bite_wait = 0;
  f->dead = 0;
  f->eaten_by = NOBODY;
  for (int j = 0; j < N_FISH; j++) { t->like[i][j] = 0.0f; t->like[j][i] = 0.0f; }
}

static float nudge(Tank *t, float value, float amount, float lo, float hi) {
  return clamp(value + rng_normal(&t->rng) * amount, lo, hi);
}

int tank_replace(Tank *t, int gone) {
  int parent = gone == 0 ? 1 : 0;
  for (int j = 0; j < N_FISH; j++)
    if (j != gone && !t->fish[j].dead && t->fish[j].score > t->fish[parent].score) parent = j;
  const Fish *mum = &t->fish[parent];

  tank_rebirth(t, gone);
  Fish *baby = &t->fish[gone];
  brain_child(&baby->brain, &mum->brain, &t->rng, 0.15f);
  baby->max_size = nudge(t, mum->max_size, 0.1f, 0.9f, 1.7f);
  baby->pace = nudge(t, mum->pace, 0.05f, 0.85f, 1.25f);
  baby->slim = nudge(t, mum->slim, 0.05f, 0.8f, 1.25f);
  baby->score = 0.0f;
  baby->eaten = baby->wall_hits = baby->bites = baby->kills = 0;
  t->like[gone][parent] = 0.5f;
  t->like[parent][gone] = 0.5f;
  return parent;
}

int tank_add_food(Tank *t, float x, float y) {
  for (int k = 0; k < N_FOOD; k++) {
    if (!t->food[k].there) {
      t->food[k].x = clamp(x, 4.0f, TANK_W - 4.0f);
      t->food[k].y = clamp(y, 4.0f, TANK_H - 4.0f);
      t->food[k].there = 1;
      return k;
    }
  }
  return -1;
}

int tank_drop_food(Tank *t) {
  float x = 12.0f + rng_unit(&t->rng) * (TANK_W - 24.0f);
  float y = 12.0f + rng_unit(&t->rng) * (TANK_H - 24.0f);
  return tank_add_food(t, x, y);
}

void tank_init(Tank *t, uint32_t seed) {
  t->rng = seed | 1u;
  t->step = 0;
  t->total_eaten = t->total_killed = t->total_starved = 0;
  for (int i = 0; i < N_FISH; i++) {
    Fish *f = &t->fish[i];
    tank_rebirth(t, i);
    fish_random_genes(f, &t->rng);
    brain_instinct(&f->brain);
    // The first fish start at all sorts of sizes and ages, not as babies.
    f->size = 0.7f + rng_unit(&t->rng) * (f->max_size - 0.7f);
    f->energy = 0.6f + 0.4f * rng_unit(&t->rng);
    f->age = NEWBORN_SAFE;
    f->score = 0.0f;
    f->eaten = f->wall_hits = f->bites = f->kills = 0;
  }
  for (int k = 0; k < N_FOOD; k++) t->food[k].there = 0;
  tank_drop_food(t);
  tank_drop_food(t);
}

static float closeness(float distance) { // 1 when touching the wall, 0 outside the wall zone
  return distance < WALL_ZONE ? 1.0f - distance / WALL_ZONE : 0.0f;
}

// Could `hunter` bite `prey` at all? (size, friendship and newborn rules; not distance)
static int edible(const Tank *t, int hunter, int prey) {
  const Fish *h = &t->fish[hunter], *p = &t->fish[prey];
  return !p->dead && p->size < h->size * PREY_RATIO && t->like[hunter][prey] < FRIEND && p->age >= NEWBORN_SAFE;
}

// Work out everything fish i can sense right now. Also reports two things used for
// scoring: how well it is swimming with the group, and whether it is being crowded.
static void sense(const Tank *t, int i, float in[N_IN], float *with_group, int *crowded, int *sees_prey) {
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
  float friend_x = 0.0f, friend_y = 0.0f, enemy_x = 0.0f, enemy_y = 0.0f;
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

    if (d2 < prey_d2 && edible(t, i, j)) { prey_d2 = d2; prey_x = ox; prey_y = oy; prey = 1; }
    if (d2 < threat_d2 && me->size < o->size * PREY_RATIO) { threat_d2 = d2; threat_x = ox; threat_y = oy; threat = 1; }
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

  // Walls: an arrow pointing away from whichever walls are near
  float wx = closeness(me->x) - closeness(TANK_W - me->x);
  float wy = closeness(me->y) - closeness(TANK_H - me->y);
  in[S_WALL_AHEAD] = wx * ax + wy * ay;
  in[S_WALL_RIGHT] = wx * rx + wy * ry;

  in[S_HUNGER] = 1.0f - me->energy;
  in[S_HURT] = 1.0f - me->health;
  in[S_ALWAYS_ON] = 1.0f;
}

// Run the brain: senses in, decisions out (each between -1 and 1).
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

static void feel(Tank *t, int who, int about, float change) {
  t->like[who][about] = clamp(t->like[who][about] + change, -1.0f, 1.0f);
}

static void die(Tank *t, int i, int eaten_by) {
  Fish *f = &t->fish[i];
  f->dead = 1;
  f->health = 0.0f;
  f->eaten_by = (uint8_t)eaten_by;
  f->score -= 20.0f;
  if (eaten_by == NOBODY) t->total_starved++;
  else t->total_killed++;
}

// Fish i wants to bite. Find something it is allowed to eat right in front of its mouth.
static void try_bite(Tank *t, int i) {
  Fish *me = &t->fish[i];
  int target = -1;
  float target_d2 = 1e9f;
  for (int j = 0; j < N_FISH; j++) {
    if (j == i || !edible(t, i, j)) continue;
    const Fish *o = &t->fish[j];
    float ox = o->x - me->x, oy = o->y - me->y;
    float reach = 6.0f * me->size + 4.0f * o->size;
    float d2 = ox * ox + oy * oy;
    if (d2 < reach * reach && ox * me->dx + oy * me->dy > 0.0f && d2 < target_d2) { target = j; target_d2 = d2; }
  }
  if (target < 0) return;

  Fish *victim = &t->fish[target];
  victim->health -= BITE_DAMAGE;
  victim->score -= 5.0f;
  me->energy = clamp(me->energy + 0.3f, 0.0f, 1.0f);
  me->size = clamp(me->size + 0.03f, 0.0f, me->max_size);
  me->bites++;
  me->bite_wait = BITE_REST;
  me->score += 8.0f;

  // Nobody forgets a bite: the victim, anyone who saw it, and above all the victim's friends.
  feel(t, target, i, -0.6f);
  for (int w = 0; w < N_FISH; w++) {
    if (w == i || w == target || t->fish[w].dead) continue;
    float ox = t->fish[w].x - victim->x, oy = t->fish[w].y - victim->y;
    if (ox * ox + oy * oy < SIGHT * SIGHT) feel(t, w, i, t->like[w][target] > FRIEND ? -0.35f : -0.15f);
  }

  if (victim->health <= 0.0f) {
    die(t, target, i);
    me->energy = 1.0f;
    me->size = clamp(me->size + 0.1f, 0.0f, me->max_size);
    me->kills++;
    me->score += 20.0f; // a successful hunt pays well, or evolution breeds fish that never hunt
  }
}

void tank_step(Tank *t) {
  float turn[N_FISH], push[N_FISH], bite[N_FISH], with_group[N_FISH];
  int crowded[N_FISH], sees_prey[N_FISH];

  // Everyone looks and decides first, then everyone moves, so no fish gets a head start.
  for (int i = 0; i < N_FISH; i++) {
    float in[N_IN], out[N_OUT];
    sense(t, i, in, &with_group[i], &crowded[i], &sees_prey[i]);
    think(&t->fish[i].brain, in, out);
    turn[i] = out[D_TURN];
    push[i] = out[D_PUSH];
    bite[i] = out[D_BITE];
  }

  for (int i = 0; i < N_FISH; i++) {
    Fish *f = &t->fish[i];

    // Turn: lean the heading towards the right (or left, if negative).
    float lean = turn[i] * TURN_RATE;
    float nx = f->dx - f->dy * lean, ny = f->dy + f->dx * lean;
    float inv = 1.0f / root(nx * nx + ny * ny);
    f->dx = nx * inv;
    f->dy = ny * inv;

    // Swim: ease towards the speed the brain asked for. Good swimmers are faster;
    // big fish and hurt fish are a little slower. A hungry fish that has decided to
    // bite and can see prey lunges, which is what lets a big fish catch a small one.
    int hunting = bite[i] > 0.0f && f->energy < BITE_HUNGER && sees_prey[i];
    float wanted = (0.6f + 0.6f * (push[i] + 1.0f)) * f->pace * (1.1f - 0.1f * f->size) * (0.7f + 0.3f * f->health);
    if (hunting) wanted *= LUNGE;
    f->speed += (wanted - f->speed) * 0.1f;
    f->x += f->dx * f->speed;
    f->y += f->dy * f->speed;

    // The glass
    int hit = 0;
    if (f->x < EDGE)          { f->x = EDGE;          if (f->dx < 0.0f) f->dx = -f->dx; hit = 1; }
    if (f->x > TANK_W - EDGE) { f->x = TANK_W - EDGE; if (f->dx > 0.0f) f->dx = -f->dx; hit = 1; }
    if (f->y < EDGE)          { f->y = EDGE;          if (f->dy < 0.0f) f->dy = -f->dy; hit = 1; }
    if (f->y > TANK_H - EDGE) { f->y = TANK_H - EDGE; if (f->dy > 0.0f) f->dy = -f->dy; hit = 1; }

    // Hunger: bigger fish burn more.
    f->energy -= DRAIN * (0.5f + 0.5f * f->size) + (hunting ? LUNGE_COST : 0.0f);
    if (f->energy < 0.0f) f->energy = 0.0f;

    // Food pellets
    float reach = 6.0f + 3.0f * f->size;
    for (int k = 0; k < N_FOOD; k++) {
      if (!t->food[k].there) continue;
      float ox = t->food[k].x - f->x, oy = t->food[k].y - f->y;
      if (ox * ox + oy * oy < reach * reach) {
        t->food[k].there = 0;
        f->energy = clamp(f->energy + MEAL, 0.0f, 1.0f);
        f->size = clamp(f->size + 0.02f, 0.0f, f->max_size);
        f->eaten++;
        t->total_eaten++;
        f->score += 10.0f;
        // Anyone else who was right there, going for the same pellet, resents it.
        for (int j = 0; j < N_FISH; j++) {
          if (j == i || t->fish[j].dead) continue;
          float qx = t->food[k].x - t->fish[j].x, qy = t->food[k].y - t->fish[j].y;
          if (qx * qx + qy * qy < 30.0f * 30.0f) feel(t, j, i, -0.12f);
        }
      }
    }

    f->score += 0.004f * with_group[i];
    if (crowded[i]) f->score -= 0.01f;
    if (hit) { f->score -= 0.05f; f->wall_hits++; }
    if (f->age < 60000) f->age++;
    if (f->bite_wait) f->bite_wait--;
  }

  // Biting
  for (int i = 0; i < N_FISH; i++) {
    const Fish *f = &t->fish[i];
    if (!f->dead && bite[i] > 0.0f && !f->bite_wait && f->energy < BITE_HUNGER) try_bite(t, i);
  }

  // Health: starving wears a fish down; a well-fed fish heals.
  for (int i = 0; i < N_FISH; i++) {
    Fish *f = &t->fish[i];
    if (f->dead) continue;
    if (f->energy <= 0.0f) {
      f->health -= 0.002f;
      if (f->health <= 0.0f) die(t, i, NOBODY);
    } else if (f->energy > 0.5f && f->health < 1.0f) {
      f->health = clamp(f->health + 0.0005f, 0.0f, 1.0f);
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
      else if (a->dx * b->dx + a->dy * b->dy > 0.7f) { feel(t, i, j, 0.0015f); feel(t, j, i, 0.0015f); }
    }
  }
  if (t->step % 16 == 0) { // feelings fade slowly if nothing keeps them going
    for (int i = 0; i < N_FISH; i++)
      for (int j = 0; j < N_FISH; j++) t->like[i][j] *= 0.997f;
  }

  // New food appears now and then, as long as there isn't much about.
  if (t->step % 90 == 0) {
    int about = 0;
    for (int k = 0; k < N_FOOD; k++) about += t->food[k].there;
    if (about < 2) tank_drop_food(t);
  }
  t->step++;
}
