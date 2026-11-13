// Checks the C code that runs the talk model (Fish/fish_talk.c) against the trained model
// itself, and prints a few lines it writes. Runs on GitHub after training.
//
// Usage: talk_test <talk_reference.txt>
// Built twice: with TALK_KV_FLOAT (must match the training exactly), and without it (the
// board's way, which rounds its working memory; checked loosely, and samples printed).

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "fish_talk.h"

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: talk_test <talk_reference.txt>\n"); return 2; }
  FILE *in = fopen(argv[1], "r");
  if (!in) { perror(argv[1]); return 1; }
  static char prompt[512];
  if (!fgets(prompt, sizeof prompt, in)) return 1;
  prompt[strcspn(prompt, "\n")] = 0;
  float expected[256], got[256];
  int n = 0;
  while (n < 256 && fscanf(in, "%f", &expected[n]) == 1) n++;
  fclose(in);

  int v = talk_score(prompt, got);
  if (v != n) { printf("PROBLEM: the model has %d characters, the reference %d\n", v, n); return 1; }
  float worst = 0.0f;
  int best_c = 0, best_py = 0;
  for (int i = 0; i < n; i++) {
    float d = fabsf(got[i] - expected[i]);
    if (d > worst) worst = d;
    if (got[i] > got[best_c]) best_c = i;
    if (expected[i] > expected[best_py]) best_py = i;
  }
#ifdef TALK_KV_FLOAT
  const float allowed = 0.02f;
  const char *how = "exact working memory";
#else
  const float allowed = 0.5f;
  const char *how = "board's rounded working memory";
#endif
  printf("%s: largest difference from the trained model %.5f (allowed %.2f); same top choice: %s\n",
         how, (double)worst, (double)allowed, best_c == best_py ? "yes" : "no");

  const char *prompts[] = {
    "BITE SHARK BOLD ANGRY STRANGER ADA: ",
    "BITTEN FRY TIMID LOYAL ENEMY GUS SAID GOT YOU ADA!: ",
    "BORN WHALE LOYAL CALM FAMILY PIP: ",
    "GREET TURTLE SOCIAL CURIOUS FRIEND MAX: ",
    "GREETED SQUID LONER EVEN FRIEND MAX SAID SEEN ANY FOOD ZOE?: ",
    "EVOLVE ORCA BOLD LONER FRIEND IVY: ",
    "MOURN RAY LOYAL CALM ENEMY JAX: ",
    "HUNGRY FRY TIMID EVEN STRANGER LEO: ",
    "CHAT SHARK ANGRY BOLD ENEMY BOB SAID YOU WILL REGRET THAT: ",
    "CHAT TURTLE SOCIAL LOYAL FRIEND IVY SAID LETS FIND FOOD: ",
  };
  for (unsigned k = 0; k < sizeof prompts / sizeof prompts[0]; k++) {
    char out[64];
    talk_say(prompts[k], out, 1234u + k);
    printf("  %s%s\n", prompts[k], out);
  }
  if (worst > allowed || (best_c != best_py && worst > allowed / 4)) { printf("PROBLEM: the C code doesn't match the trained model\n"); return 1; }
  return 0;
}
