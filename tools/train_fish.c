// Trains the fish brains. Runs on GitHub during the build (never on the board).
//
// It keeps a population of 100 brains and repeats, for as long as its time budget allows:
//   1. Put the brains into tanks of 10 sea animals of mixed kinds and let them live for a while at high speed.
//   2. Score each one: food eaten, swimming with the group, not bumping walls, biting
//      when hungry, and above all not starving or getting eaten.
//   3. Keep the best quarter. Refill the rest with children of the best: each child
//      mixes two parents' brains and adds small random changes.
// At the end it writes 10 good-but-different brains to fish_brains.h for the board.
//
// Usage: train_fish <brains file> [results file]   (TRAIN_SECONDS sets the time budget, default 40)
// The optional results file is a small JSON summary shown on the status website.

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "fish_sim.h"

#define POP    100
#define ELITE  25
#define ROUNDS 2     // each brain is tried in this many different tanks per generation
#define STEPS  1200  // ticks per try (40 seconds of tank time)
#define MAX_GENERATIONS 2000

static Brain pop[POP], next_pop[POP];
static float fit[POP];
static int order[POP], ranked[POP];
static Tank tank;
static uint32_t rng = 20261112u;
static float history_best[MAX_GENERATIONS], history_average[MAX_GENERATIONS];

// One tick of a training tank. A fish that dies comes straight back as a newborn with
// the same brain (it has already lost points for dying), so the tank stays full.
static void live_one_tick(void) {
  tank_step(&tank);
  for (int k = 0; k < N_FISH; k++)
    if (tank.fish[k].dead) tank_rebirth(&tank, k);
}

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
      tank_scatter_stages(&tank);
      for (int k = 0; k < N_FISH; k++) tank.fish[k].brain = pop[order[tk * N_FISH + k]];
      for (int s = 0; s < STEPS; s++) live_one_tick();
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
typedef struct { double food, walls, score, eaten_fish, starved, bites; } Result;

static Result measure(const char *label, const Brain brains[N_FISH]) {
  double eaten = 0, walls = 0, score = 0, killed = 0, starved = 0, bites = 0;
  const int tanks = 8, steps = 3000;
  for (int s = 0; s < tanks; s++) {
    tank_init(&tank, 1000u + (uint32_t)s * 7919u);
    tank_scatter_stages(&tank);
    for (int k = 0; k < N_FISH; k++) tank.fish[k].brain = brains[k];
    for (int i = 0; i < steps; i++) live_one_tick();
    for (int k = 0; k < N_FISH; k++) {
      eaten += tank.fish[k].eaten;
      walls += tank.fish[k].wall_hits;
      score += tank.fish[k].score;
      bites += tank.fish[k].bites; // (bites by fish that later died aren't counted)
    }
    killed += tank.total_killed;
    starved += tank.total_starved;
  }
  double n = (double)tanks * N_FISH;
  printf("%-16s per fish in 100 s: food %.2f, wall bumps %.1f, score %.1f | per tank: %.2f bites, %.2f fish eaten, %.2f starved\n",
         label, eaten / n, walls / n, score / n, bites / tanks, killed / tanks, starved / tanks);
  Result result = {eaten / n, walls / n, score / n, killed / tanks, starved / tanks, bites / tanks};
  return result;
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
    float mean = 0;
    for (int p = 0; p < POP; p++) mean += fit[p];
    mean /= POP;
    history_best[generation] = fit[ranked[0]] / ROUNDS; // per try, so it compares with the table below
    history_average[generation] = mean / ROUNDS;
    generation++;
    if (generation == 1 || generation % 20 == 0)
      printf("generation %4d: best score %.1f, average %.1f\n", generation, (double)fit[ranked[0]], (double)mean);
    if (generation >= MAX_GENERATIONS || time(NULL) - start >= budget) break;
    breed();
  }
  long seconds = (long)(time(NULL) - start);
  printf("trained for %d generations in %ld s\n", generation, seconds);

  // Pick 10 for the board. One generation's score is partly luck (which tank a brain landed
  // in), so the top 30 sit a longer final exam: ten more tries each, in tanks made up of
  // finalists only. The 10 with the best exam results go to the board.
  enum { FINALISTS = 30, EXAM_ROUNDS = 10 };
  static float exam[FINALISTS];
  int seat[FINALISTS], chosen[N_FISH];
  for (int f = 0; f < FINALISTS; f++) exam[f] = 0.0f;
  for (int r = 0; r < EXAM_ROUNDS; r++) {
    for (int f = 0; f < FINALISTS; f++) seat[f] = f;
    for (int f = FINALISTS - 1; f > 0; f--) {
      int q = (int)(rng_next(&rng) % (uint32_t)(f + 1));
      int tmp = seat[f]; seat[f] = seat[q]; seat[q] = tmp;
    }
    for (int tk = 0; tk < FINALISTS / N_FISH; tk++) {
      tank_init(&tank, rng_next(&rng));
      tank_scatter_stages(&tank);
      for (int k = 0; k < N_FISH; k++) tank.fish[k].brain = pop[ranked[seat[tk * N_FISH + k]]];
      for (int st = 0; st < STEPS; st++) live_one_tick();
      for (int k = 0; k < N_FISH; k++) exam[seat[tk * N_FISH + k]] += tank.fish[k].score;
    }
  }
  for (int k = 0; k < N_FISH; k++) { // the ten best exam results
    int best = -1;
    for (int f = 0; f < FINALISTS; f++) {
      int taken = 0;
      for (int j = 0; j < k; j++) if (chosen[j] == ranked[f]) taken = 1;
      if (!taken && (best < 0 || exam[f] > exam[best])) best = f;
    }
    chosen[k] = ranked[best];
  }

  Brain before[N_FISH], after[N_FISH];
  for (int k = 0; k < N_FISH; k++) { before[k] = instinct; after[k] = pop[chosen[k]]; }
  Result r_before = measure("instincts only:", before);
  Result r_after = measure("trained brains:", after);

  // Training is not allowed to make the fish worse. If the trained brains don't beat the
  // hand-written instincts, the board gets the instincts instead, each with a small random
  // difference so the fish still aren't identical.
  int kept_instincts = r_after.score < r_before.score;
  if (kept_instincts) {
    printf("training did not beat the instincts this time: using the instincts\n");
    after[0] = instinct;
    for (int k = 1; k < N_FISH; k++) brain_child(&after[k], &instinct, &rng, 0.04f);
    r_after = measure("brains used:", after);
  }

  if (argc > 2) {
    FILE *js = fopen(argv[2], "w");
    if (!js) { perror(argv[2]); return 1; }
    fprintf(js, "{\"generations\": %d, \"seconds\": %ld, \"kept_instincts\": %d,\n", generation, seconds, kept_instincts);
    // Progress, thinned to about 150 points. Each point is the average over a small
    // window of generations, because one generation on its own is very jumpy.
    int window = generation / 150 + 1;
    fprintf(js, " \"steps_per_try\": %d, \"history\": [", STEPS);
    for (int g = 0, first = 1; g + window <= generation; g += window, first = 0) {
      double best = 0, average = 0;
      for (int k = 0; k < window; k++) { best += history_best[g + k]; average += history_average[g + k]; }
      fprintf(js, "%s[%d, %.2f, %.2f]", first ? "" : ", ", g + window, best / window, average / window);
    }
    fprintf(js, "],\n");
    const Result *rs[2] = {&r_before, &r_after};
    const char *names[2] = {"instinct", "trained"};
    for (int k = 0; k < 2; k++)
      fprintf(js, " \"%s\": {\"food\": %.3f, \"walls\": %.3f, \"score\": %.3f, \"eaten_fish\": %.3f, \"starved\": %.3f}%s\n",
              names[k], rs[k]->food, rs[k]->walls, rs[k]->score, rs[k]->eaten_fish, rs[k]->starved, k ? "}" : ",");
    fclose(js);
  }

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
