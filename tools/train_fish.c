// Trains the fish brains. Runs on GitHub during the build (never on the board).
//
// It keeps a population of 96 brains and repeats, for as long as its time budget allows:
//   1. Put the brains into tanks of 12 and let them live for a while at high speed.
//   2. Score each one: food eaten, swimming with the group, not bumping walls or fish.
//   3. Keep the best quarter. Refill the rest with children of the best: each child
//      mixes two parents' brains and adds small random changes.
// At the end it writes 12 good-but-different brains to fish_brains.h for the board.
//
// Usage: train_fish <output file>      (TRAIN_SECONDS sets the time budget, default 40)

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "fish_sim.h"

#define POP    96
#define ELITE  24
#define ROUNDS 2     // each brain is tried in this many different tanks per generation
#define STEPS  1200  // ticks per try (40 seconds of tank time)
#define MAX_GENERATIONS 2000

static Brain pop[POP], next_pop[POP];
static float fit[POP];
static int order[POP], ranked[POP];
static Tank tank;
static uint32_t rng = 20261112u;

static void evaluate(void) {
  for (int p = 0; p < POP; p++) fit[p] = 0.0f;
  for (int r = 0; r < ROUNDS; r++) {
    for (int p = 0; p < POP; p++) order[p] = p;
    for (int p = POP - 1; p > 0; p--) { // shuffle, so every tank is a fresh mix
      int q = (int)(rng_next(&rng) % (uint32_t)(p + 1));
      int tmp = order[p]; order[p] = order[q]; order[q] = tmp;
    }
    for (int tk = 0; tk < POP / N_FISH; tk++) {
      tank_init(&tank, rng_next(&rng));
      for (int k = 0; k < N_FISH; k++) tank.fish[k].brain = pop[order[tk * N_FISH + k]];
      for (int s = 0; s < STEPS; s++) tank_step(&tank);
      for (int k = 0; k < N_FISH; k++) fit[order[tk * N_FISH + k]] += tank.fish[k].score;
    }
  }
}

static int by_fitness(const void *a, const void *b) {
  float d = fit[*(const int *)b] - fit[*(const int *)a];
  return d > 0 ? 1 : d < 0 ? -1 : 0;
}

static void breed(void) {
  for (int e = 0; e < ELITE; e++) next_pop[e] = pop[ranked[e]];
  for (int c = ELITE; c < POP; c++) {
    const Brain *mum = &pop[ranked[rng_next(&rng) % ELITE]];
    const Brain *dad = &pop[ranked[rng_next(&rng) % ELITE]];
    for (int w = 0; w < N_WEIGHTS; w++) {
      float v = (rng_next(&rng) & 1u) ? mum->w[w] : dad->w[w];
      if (rng_unit(&rng) < 0.25f) v += rng_normal(&rng) * 0.12f;
      next_pop[c].w[w] = v;
    }
  }
  for (int p = 0; p < POP; p++) pop[p] = next_pop[p];
}

// Fair comparison: the same 8 starting tanks, 100 seconds of tank time each.
static void measure(const char *label, const Brain brains[N_FISH]) {
  double eaten = 0, walls = 0, score = 0;
  const int tanks = 8, steps = 3000;
  for (int s = 0; s < tanks; s++) {
    tank_init(&tank, 1000u + (uint32_t)s * 7919u);
    for (int k = 0; k < N_FISH; k++) tank.fish[k].brain = brains[k];
    for (int i = 0; i < steps; i++) tank_step(&tank);
    for (int k = 0; k < N_FISH; k++) {
      eaten += tank.fish[k].eaten;
      walls += tank.fish[k].wall_hits;
      score += tank.fish[k].score;
    }
  }
  double n = (double)tanks * N_FISH;
  printf("%-16s per fish in 100 s: food %.2f, wall bumps %.1f, score %.1f\n", label, eaten / n, walls / n, score / n);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: train_fish <output file>\n"); return 2; }
  const char *env = getenv("TRAIN_SECONDS");
  long budget = env ? atol(env) : 40;

  // Start everyone from the hand-written instincts, with more and more random change.
  Brain instinct;
  brain_instinct(&instinct);
  pop[0] = instinct;
  for (int p = 1; p < POP; p++) brain_child(&pop[p], &instinct, &rng, 0.05f + 0.5f * (float)p / POP);

  time_t start = time(NULL);
  int generation = 0;
  for (;;) {
    evaluate();
    for (int p = 0; p < POP; p++) ranked[p] = p;
    qsort(ranked, POP, sizeof ranked[0], by_fitness);
    generation++;
    if (generation == 1 || generation % 20 == 0) {
      float mean = 0;
      for (int p = 0; p < POP; p++) mean += fit[p];
      printf("generation %4d: best score %.1f, average %.1f\n", generation, (double)fit[ranked[0]], (double)(mean / POP));
    }
    if (generation >= MAX_GENERATIONS || time(NULL) - start >= budget) break;
    breed();
  }
  printf("trained for %d generations in %ld s\n", generation, (long)(time(NULL) - start));

  // Pick 12 for the board: the best one, then whichever of the top 32 is most unlike
  // those already picked, so the fish have different characters.
  int chosen[N_FISH], n_chosen = 1;
  chosen[0] = ranked[0];
  while (n_chosen < N_FISH) {
    int pick = -1;
    float pick_gap = -1.0f;
    for (int c = 1; c < 32; c++) {
      int cand = ranked[c], taken = 0;
      float gap = 1e30f;
      for (int k = 0; k < n_chosen; k++) {
        if (chosen[k] == cand) taken = 1;
        float d = 0;
        for (int w = 0; w < N_WEIGHTS; w++) {
          float diff = pop[cand].w[w] - pop[chosen[k]].w[w];
          d += diff * diff;
        }
        if (d < gap) gap = d;
      }
      if (!taken && gap > pick_gap) { pick_gap = gap; pick = cand; }
    }
    chosen[n_chosen++] = pick;
  }

  Brain before[N_FISH], after[N_FISH];
  for (int k = 0; k < N_FISH; k++) { before[k] = instinct; after[k] = pop[chosen[k]]; }
  measure("instincts only:", before);
  measure("trained brains:", after);

  FILE *out = fopen(argv[1], "w");
  if (!out) { perror(argv[1]); return 1; }
  fprintf(out, "// Made by tools/train_fish.c during the GitHub build. Don't edit: it is replaced every build.\n");
  fprintf(out, "#define FISH_BRAINS_GENERATIONS %d\n", generation);
  fprintf(out, "static const float FISH_BRAINS[N_FISH][N_WEIGHTS] = {\n");
  for (int k = 0; k < N_FISH; k++) {
    fprintf(out, "  {");
    for (int w = 0; w < N_WEIGHTS; w++) fprintf(out, "%.6ff,%s", (double)after[k].w[w], w % 8 == 7 ? "\n   " : " ");
    fprintf(out, "},\n");
  }
  fprintf(out, "};\n");
  fclose(out);
  printf("wrote %s\n", argv[1]);
  return 0;
}
