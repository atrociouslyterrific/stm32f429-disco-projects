// The fish tank's rules: what a fish can sense, how its brain turns that into movement,
// and what happens when it meets food or another fish. No screen code in here, so the very
// same file runs on the board (to show the fish) and on GitHub (to train their brains).
#ifndef FISH_SIM_H
#define FISH_SIM_H

#include <stdint.h>

#define TANK_W 240.0f
#define TANK_H 224.0f // the rest of the screen shows each fish's health and hunger

#define N_FISH 10
#define N_FOOD 8
#define NOBODY N_FISH

// Each fish's brain is a small neural network:
//   21 senses -> 8 "hidden" cells -> 3 decisions (turn, how hard to swim, whether to bite)
// The brain is nothing but a list of numbers (weights). Different numbers, different fish.
#define N_IN  21
#define N_HID 8
#define N_OUT 3
#define N_WEIGHTS (N_HID * N_IN + N_OUT * (N_HID + 1))

typedef struct {
  float w[N_WEIGHTS];
} Brain;

typedef struct {
  float x, y;     // where it is
  float dx, dy;   // which way it faces (a direction of length 1)
  float speed;
  float energy;   // 1 = full, 0 = empty. Hunger is 1 - energy.
  float health;   // 1 = fine, 0 = dead. Falls when starving or bitten, heals when well fed.
  float size;     // how big it is right now (1 = average); grows as it eats

  // What it was born with ("genes"):
  float max_size; // how big it can grow
  float pace;     // how fast a swimmer it is
  float slim;     // body shape: above 1 long and thin, below 1 short and round

  float score;    // how well it is doing; decides who has babies
  uint16_t eaten, wall_hits, bites, kills;
  uint16_t age;        // ticks alive
  uint8_t bite_wait;   // ticks until it can bite again
  uint8_t dead;        // set when its health runs out; whoever runs the tank replaces it
  uint8_t eaten_by;    // which fish finished it off, or NOBODY if it starved
  Brain brain;
} Fish;

typedef struct {
  float x, y;
  uint8_t there;
} Food;

typedef struct {
  Fish fish[N_FISH];
  Food food[N_FOOD];
  // Relationships: like[a][b] is how fish a feels about fish b,
  // from -1 (enemy) through 0 (stranger) to +1 (friend). It needn't be mutual.
  float like[N_FISH][N_FISH];
  uint32_t rng;
  uint32_t step;
  uint32_t total_eaten;  // food pellets eaten
  uint32_t total_killed; // fish eaten by other fish
  uint32_t total_starved;
} Tank;

// Random numbers (the same sequence for the same starting seed).
uint32_t rng_next(uint32_t *state);
float rng_unit(uint32_t *state);   // between 0 and 1
float rng_normal(uint32_t *state); // mostly near 0, sometimes further out

// A hand-written starter brain: basic instincts. Training starts from this and improves it.
void brain_instinct(Brain *brain);

// Copy a parent's brain with small random changes. amount = how big the changes are.
void brain_child(Brain *child, const Brain *parent, uint32_t *rng, float amount);

void fish_random_genes(Fish *fish, uint32_t *rng);
void tank_init(Tank *tank, uint32_t seed);
int tank_drop_food(Tank *tank);                   // at a random spot; returns the slot or -1
int tank_add_food(Tank *tank, float x, float y);  // at a chosen spot
// Bring fish i back as a small newborn somewhere random, forgetting its relationships.
// Its brain and genes are left alone; change them afterwards if it should be a new fish.
void tank_rebirth(Tank *tank, int i);
// Fish `gone` has died: the fish doing best has a baby in its place, with a slightly
// changed copy of its brain and genes. They start out as friends. Returns the parent.
int tank_replace(Tank *tank, int gone);
void tank_step(Tank *tank); // move time forward one tick

#endif
