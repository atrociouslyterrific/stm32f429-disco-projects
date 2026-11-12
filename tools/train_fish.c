// Trains the fish brains. Runs on GitHub (never on the board, never on your own computer).
//
// It keeps a population of 100 brains and repeats, for as long as its time budget allows:
//   1. Put the brains into tanks of 10 sea animals of mixed kinds and let them live for a
//      while at high speed.
//   2. Score each one: food eaten, swimming with the group, not bumping walls, biting
//      when hungry, and above all not starving or getting eaten.
//   3. Keep the best quarter. Refill the rest with children of the best: each child
//      mixes two parents' brains and adds small random changes.
// At the end the best brains sit a longer final exam, and the top 10 go to the board.
//
// Training can carry on from where an earlier run stopped (--population), and reports how
// it is getting on while it runs (--progress), which is what the website's live view shows.
//
// Usage: train_fish --brains <file.h> [--results <file.json>] [--population <file.bin>]
//                   [--progress <file.json>]
//   --brains      where to write the 10 brains for the board
//   --results     a JSON summary for the website, written at the end
//   --population  read the whole population from here if it exists, and save it back at the end
//   --progress    a JSON progress report, rewritten every few seconds while training
// TRAIN_SECONDS sets the time budget (default 40).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fish_sim.h"

#define POP    100
#define ELITE  25
#define ROUNDS 2     // each brain is tried in this many different tanks per generation
#define STEPS  1200  // ticks per try (40 seconds of tank time)
#define HISTORY_MAX 4096
#define HISTORY_KEPT 300 // points remembered from one run to the next

static Brain pop[POP], next_pop[POP];
static float fit[POP];
static int order[POP], ranked[POP];
static Tank tank;
static uint32_t rng = 20261112u;

// Progress so far: for a generation (counted across every run), the best and average score.
typedef struct { uint32_t generation; float best, average; } Point;
static Point history[HISTORY_MAX];
static int n_history;
static uint32_t generations_before; // generations done by earlier runs

typedef struct { double food, walls, score, eaten_fish, starved, bites; } Result;

// One tick of a training tank. An animal that dies comes straight back as a newborn with
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
      bites += tank.fish[k].bites; // (bites by animals that later died aren't counted)
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

// ---------- remembering progress ----------

// Squeeze the history down to at most `limit` points by averaging neighbours together
// (one generation on its own is very jumpy anyway).
static void thin_history(int limit) {
  while (n_history > limit) {
    int out = 0;
    for (int i = 0; i + 1 < n_history; i += 2) {
      history[out].generation = history[i + 1].generation;
      history[out].best = (history[i].best + history[i + 1].best) / 2;
      history[out].average = (history[i].average + history[i + 1].average) / 2;
      out++;
    }
    if (n_history & 1) history[out++] = history[n_history - 1];
    n_history = out;
  }
}

static void remember(uint32_t generation, float best, float average) {
  if (n_history == HISTORY_MAX) thin_history(HISTORY_MAX / 2);
  history[n_history].generation = generation;
  history[n_history].best = best;
  history[n_history].average = average;
  n_history++;
}

// The population file: a short header, the history, then every brain.
#define FILE_MAGIC 0x504F5046u // "FPOP"
typedef struct { uint32_t magic, version, n_weights, pop, generations, n_history; } FileHeader;

static int load_population(const char *path) {
  FILE *in = fopen(path, "rb");
  if (!in) return 0;
  FileHeader h;
  int ok = fread(&h, sizeof h, 1, in) == 1 && h.magic == FILE_MAGIC && h.version == 1 &&
           h.n_weights == N_WEIGHTS && h.pop == POP && h.n_history <= HISTORY_KEPT &&
           fread(history, sizeof(Point), h.n_history, in) == h.n_history &&
           fread(pop, sizeof(Brain), POP, in) == POP;
  fclose(in);
  if (!ok) { printf("%s doesn't fit the current brains: starting again from the instincts\n", path); return 0; }
  n_history = (int)h.n_history;
  generations_before = h.generations;
  printf("carrying on from %s: %u generations so far\n", path, (unsigned)h.generations);
  return 1;
}

static void save_population(const char *path, uint32_t generations) {
  thin_history(HISTORY_KEPT);
  FILE *out = fopen(path, "wb");
  if (!out) { perror(path); exit(1); }
  FileHeader h = {FILE_MAGIC, 1, N_WEIGHTS, POP, generations, (uint32_t)n_history};
  fwrite(&h, sizeof h, 1, out);
  fwrite(history, sizeof(Point), (size_t)n_history, out);
  fwrite(pop, sizeof(Brain), POP, out);
  fclose(out);
}

// Write the JSON the website reads. `before` and `after` are only known at the end.
static void write_report(const char *path, const char *state, uint32_t generations, int run_generations,
                         long seconds, long budget, int kept_instincts, const Result *before, const Result *after) {
  char tmp[1024];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *js = fopen(tmp, "w");
  if (!js) { perror(tmp); exit(1); }
  fprintf(js, "{\"state\": \"%s\", \"generations\": %u, \"run_generations\": %d, \"seconds\": %ld, \"budget\": %ld,\n",
          state, (unsigned)generations, run_generations, seconds, budget);
  fprintf(js, " \"updated\": %ld, \"kept_instincts\": %d, \"steps_per_try\": %d,\n", (long)time(NULL), kept_instincts, STEPS);

  // About 200 points of history, each the average of a small window.
  int window = n_history / 200 + 1;
  fprintf(js, " \"history\": [");
  for (int g = 0, first = 1; g + window <= n_history; g += window, first = 0) {
    double best = 0, average = 0;
    for (int k = 0; k < window; k++) { best += history[g + k].best; average += history[g + k].average; }
    fprintf(js, "%s[%u, %.2f, %.2f]", first ? "" : ", ", (unsigned)history[g + window - 1].generation, best / window, average / window);
  }
  fprintf(js, "]");
  if (before && after) {
    const Result *rs[2] = {before, after};
    const char *names[2] = {"instinct", "trained"};
    for (int k = 0; k < 2; k++)
      fprintf(js, ",\n \"%s\": {\"food\": %.3f, \"walls\": %.3f, \"score\": %.3f, \"eaten_fish\": %.3f, \"starved\": %.3f}",
              names[k], rs[k]->food, rs[k]->walls, rs[k]->score, rs[k]->eaten_fish, rs[k]->starved);
  }
  fprintf(js, "}\n");
  fclose(js);
  rename(tmp, path); // swapped in whole, so nobody ever reads a half-written file
}

int main(int argc, char **argv) {
  const char *brains_path = NULL, *results_path = NULL, *population_path = NULL, *progress_path = NULL;
  for (int a = 1; a + 1 < argc; a += 2) {
    if (!strcmp(argv[a], "--brains")) brains_path = argv[a + 1];
    else if (!strcmp(argv[a], "--results")) results_path = argv[a + 1];
    else if (!strcmp(argv[a], "--population")) population_path = argv[a + 1];
    else if (!strcmp(argv[a], "--progress")) progress_path = argv[a + 1];
    else { fprintf(stderr, "unknown option %s\n", argv[a]); return 2; }
  }
  if (!brains_path) { fprintf(stderr, "usage: train_fish --brains <file.h> [--results f.json] [--population f.bin] [--progress f.json]\n"); return 2; }
  const char *env = getenv("TRAIN_SECONDS");
  long budget = env ? atol(env) : 40;

  Brain instinct;
  brain_instinct(&instinct);
  if (!population_path || !load_population(population_path)) {
    // Start everyone from the hand-written instincts, with more and more random change.
    pop[0] = instinct;
    for (int p = 1; p < POP; p++) brain_child(&pop[p], &instinct, &rng, 0.05f + 0.5f * (float)p / POP);
  }
  rng ^= (uint32_t)time(NULL) * 2654435761u; // a different shuffle every run
  rng |= 1u;

  time_t start = time(NULL), last_report = 0;
  int generation = 0;
  for (;;) {
    evaluate();
    for (int p = 0; p < POP; p++) ranked[p] = p;
    qsort(ranked, POP, sizeof ranked[0], by_fitness);
    float mean = 0;
    for (int p = 0; p < POP; p++) mean += fit[p];
    mean /= POP;
    generation++;
    remember(generations_before + (uint32_t)generation, fit[ranked[0]] / ROUNDS, mean / ROUNDS); // per try, so it compares with the table
    if (generation == 1 || generation % 50 == 0)
      printf("generation %6u: best score %.1f, average %.1f\n", (unsigned)(generations_before + (uint32_t)generation), (double)fit[ranked[0]], (double)mean);

    time_t now = time(NULL);
    if (progress_path && now - last_report >= 4) {
      write_report(progress_path, "training", generations_before + (uint32_t)generation, generation, (long)(now - start), budget, 0, NULL, NULL);
      last_report = now;
    }
    if (now - start >= budget) break;
    breed();
  }
  long seconds = (long)(time(NULL) - start);
  uint32_t generations = generations_before + (uint32_t)generation;
  printf("trained for %d generations in %ld s (%u in total)\n", generation, seconds, (unsigned)generations);

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

  // Training is not allowed to make the animals worse. If the trained brains don't beat the
  // hand-written instincts, the board gets the instincts instead, each with a small random
  // difference so the animals still aren't identical. (The population keeps evolving
  // either way, so a later run can still overtake the instincts.)
  int kept_instincts = r_after.score < r_before.score;
  if (kept_instincts) {
    printf("training did not beat the instincts this time: using the instincts\n");
    after[0] = instinct;
    for (int k = 1; k < N_FISH; k++) brain_child(&after[k], &instinct, &rng, 0.04f);
    r_after = measure("brains used:", after);
  }

  if (population_path) save_population(population_path, generations);
  if (results_path) write_report(results_path, "done", generations, generation, seconds, budget, kept_instincts, &r_before, &r_after);

  FILE *out = fopen(brains_path, "w");
  if (!out) { perror(brains_path); return 1; }
  fprintf(out, "// Made by tools/train_fish.c on GitHub. Don't edit: it is replaced by every training run.\n");
  fprintf(out, "#define FISH_BRAINS_GENERATIONS %u\n", (unsigned)generations);
  fprintf(out, "#define FISH_BRAINS_WEIGHTS %d\n", N_WEIGHTS);
  fprintf(out, "#define FISH_BRAINS_FISH %d\n", N_FISH);
  fprintf(out, "static const float FISH_BRAINS[N_FISH][N_WEIGHTS] = {\n");
  for (int k = 0; k < N_FISH; k++) {
    fprintf(out, "  {");
    for (int w = 0; w < N_WEIGHTS; w++) fprintf(out, "%.6ff,%s", (double)after[k].w[w], w % 8 == 7 ? "\n   " : " ");
    fprintf(out, "},\n");
  }
  fprintf(out, "};\n");
  fclose(out);
  printf("wrote %s\n", brains_path);
  return 0;
}
