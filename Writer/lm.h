// The Writer's language model: reads text one character at a time and writes more.
// Trained on GitHub (tools/train_writer.py); the trained numbers are in lm_model.h. The same
// code runs on the board and, compiled for web browsers, on the website.
#ifndef LM_H
#define LM_H

#include <stdint.h>

int lm_available(void);   // 0 until a model has been trained
int lm_memory(void);      // how many characters it can look back over
void lm_reset(void);      // forget what it has read
int lm_known(char c);     // 1 if it knows this character
void lm_feed(char c);     // read one more character (after this, lm_pos() goes up by one)
int lm_pos(void);         // how many characters it has read since the last reset
// Pick the next character, from what it has read so far (doesn't read it: feed it after).
// temperature: lower is safer and more repetitive, higher more adventurous.
char lm_next(float temperature, uint32_t *rng);

#ifdef LM_TOOLS // for checking the model on GitHub
int lm_score(const char *prompt, float *scores);
#endif

#endif
