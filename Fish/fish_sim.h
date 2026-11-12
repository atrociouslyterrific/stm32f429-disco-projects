// The fish tank's rules: what a fish can sense, how its brain turns that into movement,
// and what happens when it reaches food. No screen code in here, so the very same file
// runs on the board (to show the fish) and on GitHub (to train their brains at high speed).
#ifndef FISH_SIM_H
#define FISH_SIM_H

#include <stdint.h>

#define TANK_W 240.0f
#define TANK_H 296.0f // the bottom strip of the screen is kept for the status line

#define N_FISH 12
#define N_FOOD 5

// Each fish's brain is a small neural network:
//   12 senses -> 6 "hidden" cells -> 2 decisions (turn, and how hard to swim)
// The brain is nothing but a list of numbers (weights). Different numbers, different fish.
#define N_IN  12
#define N_HID 6
#define N_OUT 2
#define N_WEIGHTS (N_HID * N_IN + N_OUT * (N_HID + 1))

typedef struct {
  float w[N_WEIGHTS];
} Brain;

typedef struct {
  float x, y;     // where it is
  float dx, dy;   // which way it faces (a direction of length 1)
  float speed;
  float energy;   // 1 = full, 0 = starving
  float score;    // how well it is doing (food eaten, schooling, not bumping walls)
  uint16_t eaten;
  uint16_t wall_hits;
  Brain brain;
} Fish;

typedef struct {
  float x, y;
  uint8_t there;
} Food;

typedef struct {
  Fish fish[N_FISH];
  Food food[N_FOOD];
  uint32_t rng;
  uint32_t step;
  uint32_t total_eaten;
} Tank;

// Random numbers (the same sequence for the same starting seed).
uint32_t rng_next(uint32_t *state);
float rng_unit(uint32_t *state);   // between 0 and 1
float rng_normal(uint32_t *state); // mostly near 0, sometimes further out

// A hand-written starter brain: basic instincts (go to food, stay with the others,
// don't swim into walls). Training starts from this and improves on it.
void brain_instinct(Brain *brain);

// Copy a parent's brain with small random changes. amount = how big the changes are.
void brain_child(Brain *child, const Brain *parent, uint32_t *rng, float amount);

void fish_place(Fish *fish, uint32_t *rng); // somewhere random, facing a random way
void tank_init(Tank *tank, uint32_t seed);
int tank_drop_food(Tank *tank);             // returns which food slot, or -1 if full
void tank_step(Tank *tank);                 // move time forward one tick

#endif
