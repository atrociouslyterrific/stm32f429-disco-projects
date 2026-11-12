// Do the downloaded brains fit the current rules? (If the number of senses, cells or
// animals has changed since they were trained, they don't, and compiling this fails.)
#include "fish_sim.h"
#include "fish_brains.h"
_Static_assert(FISH_BRAINS_WEIGHTS == N_WEIGHTS, "brains were trained for a different size of brain");
_Static_assert(FISH_BRAINS_FISH == N_FISH, "brains were trained for a different number of animals");
int main(void) { return (int)FISH_BRAINS[0][0] * 0; }
