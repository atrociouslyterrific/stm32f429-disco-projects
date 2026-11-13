// Checks the C code that runs the Writer's model (Writer/lm.c) against the trained model, and
// prints a little of what it writes. Runs on GitHub after training. Built twice: with
// LM_KV_FLOAT (must match the training exactly) and without (the board's rounded way).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "lm.h"

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  FILE *in = fopen(argv[1], "r");
  if (!in) { perror(argv[1]); return 1; }
  static char prompt[512];
  if (!fgets(prompt, sizeof prompt, in)) return 1;
  prompt[strcspn(prompt, "\n")] = 0;
  float expected[256], got[256];
  int n = 0;
  while (n < 256 && fscanf(in, "%f", &expected[n]) == 1) n++;
  fclose(in);
  int v = lm_score(prompt, got);
  if (v != n) { printf("PROBLEM: the model has %d characters, the reference %d\n", v, n); return 1; }
  float worst = 0.0f;
  int a = 0, b = 0;
  for (int i = 0; i < n; i++) {
    if (fabsf(got[i] - expected[i]) > worst) worst = fabsf(got[i] - expected[i]);
    if (got[i] > got[a]) a = i;
    if (expected[i] > expected[b]) b = i;
  }
#ifdef LM_KV_FLOAT
  const float allowed = 0.02f;
#else
  const float allowed = 0.6f;
#endif
  printf("largest difference from the trained model %.5f (allowed %.2f); same top choice: %s\n", (double)worst, (double)allowed, a == b ? "yes" : "no");
  const char *starts[] = {"ONCE UPON A TIME", "THE WOLF SAID", "THE PRINCESS"};
  for (unsigned k = 0; k < 3; k++) {
    uint32_t rng = 777u + k;
    lm_reset();
    for (const char *p = starts[k]; *p; p++) lm_feed(*p);
    char out[200];
    int w = 0;
    while (w < 150 && lm_pos() < lm_memory()) { char c = lm_next(0.75f, &rng); out[w++] = c == '\n' ? '/' : c; lm_feed(c); }
    out[w] = 0;
    printf("  %s%s\n", starts[k], out);
  }
  if (worst > allowed || (a != b && worst > allowed / 4)) { printf("PROBLEM: the C code doesn't match the trained model\n"); return 1; }
  return 0;
}
