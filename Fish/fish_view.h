// A snapshot of the tank as one flat list of numbers.
//
// The website draws tanks from this list, whether the numbers come from its own copy of
// the rules running in the browser, or from a real board over USB. Keeping the layout in
// one place means the board and the website can never disagree about what goes where.
#ifndef FISH_VIEW_H
#define FISH_VIEW_H

#include "fish_sim.h"

#define VIEW_HEADER   8  // step, food eaten, fish eaten, fish starved, born, nutrients, (2 spare)
#define VIEW_PER_FISH 16 // x, y, dx, dy, size, slim, energy, health, score, age, kills, eaten, step of evolution, meal points, most health it can have, kind of animal
#define VIEW_FISH_AT  VIEW_HEADER
#define VIEW_FOOD_AT  (VIEW_FISH_AT + N_FISH * VIEW_PER_FISH) // x, y, there   for each food slot
#define VIEW_LIKE_AT  (VIEW_FOOD_AT + N_FOOD * 3)             // N_FISH x N_FISH feelings
#define VIEW_PLANT_AT (VIEW_LIKE_AT + N_FISH * N_FISH)        // x, height     for each plant
#define VIEW_SIZE     (VIEW_PLANT_AT + N_PLANTS * 2)

void fish_view(const Tank *tank, unsigned born, float out[VIEW_SIZE]);

#endif
