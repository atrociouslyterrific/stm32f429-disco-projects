// A snapshot of the tank as one flat list of numbers.
//
// The website draws tanks from this list, whether the numbers come from its own copy of
// the rules running in the browser, or from a real board over USB. Keeping the layout in
// one place means the board and the website can never disagree about what goes where.
#ifndef FISH_VIEW_H
#define FISH_VIEW_H

#include "fish_sim.h"

#define VIEW_HEADER   8  // step, food eaten, fish eaten, fish starved, born, nutrients, (2 spare)
// Per animal:
//   0 x, 1 y, 2 dx, 3 dy, 4 size, 5 slim, 6 energy, 7 health, 8 score, 9 age, 10 kills,
//   11 eaten, 12 step of evolution, 13 meal points, 14 most health it can have, 15 kind of animal,
//   16 id, 17 mum's id, 18 dad's id, 19 family, 20 generation,
//   21 pace, 22 bulk, 23 thrift, 24 boldness, 25 red, 26 green, 27 blue, 28 pattern,
//   29 sociability, 30 aggression, 31 loyalty, 32 curiosity
#define VIEW_PER_FISH 33
#define VIEW_FISH_AT  VIEW_HEADER
#define VIEW_FOOD_AT  (VIEW_FISH_AT + N_FISH * VIEW_PER_FISH) // x, y, there   for each food slot
#define VIEW_LIKE_AT  (VIEW_FOOD_AT + N_FOOD * 3)             // N_FISH x N_FISH feelings
#define VIEW_PLANT_AT (VIEW_LIKE_AT + N_FISH * N_FISH)        // x, height     for each plant
#define VIEW_SIZE     (VIEW_PLANT_AT + N_PLANTS * 2)

void fish_view(const Tank *tank, unsigned born, float out[VIEW_SIZE]);

#endif
