// The animals talk. Each line is written, one character at a time, by a small language
// model trained on GitHub (tools/train_talk.py) and stored in talk_model.h. The same code
// runs on the board and, compiled for web browsers, on the website.
//
// When something happens in the tank (a bite, a fight, a birth, a new friendship or feud,
// an animal evolving or being eaten, or just two animals meeting), the animal involved
// says something, in character: the model is told what happened, what kind of animal is
// speaking, its personality, and how it feels about who it is talking to. The other animal
// then replies, having "heard" what was said.
#ifndef FISH_TALK_H
#define FISH_TALK_H

#include "fish_sim.h"

#define TALK_LINE 34 // the most characters in one line (fits the board's log after "ADA: ")

typedef struct {
  uint8_t speaker, listener;     // animal numbers (0 .. N_FISH-1)
  char text[TALK_LINE + 1];
} TalkLine;

int talk_available(void);              // 0 until a talk model has been trained
void talk_init(uint32_t seed);
void talk_set_name(int i, const char *name); // the first three letters are used
void talk_quiet(int on);               // 1: notice nothing new (e.g. while skipping ahead)

// Call after tank_step (before dead animals are replaced, so their friends can mourn them),
// and again after the replacements (to notice births).
void talk_notice(const Tank *tank);

// Write up to `chars` more characters (feeding the model what's happening counts too).
// On the board each character takes a few milliseconds, so it is done a little at a time.
void talk_work(const Tank *tank, int chars);

int talk_take(TalkLine *line);         // 1 and a finished line, or 0 if none is waiting
// Who is speaking right now and what they have said so far (for a "typing" bubble).
// Returns 0 if nobody is.
int talk_current(int *speaker, int *listener, char *so_far);

#ifdef TALK_TOOLS // for checking the model on GitHub
int talk_score(const char *prompt, float *scores); // scores for the next character; returns how many
void talk_say(const char *prompt, char *out, uint32_t seed);
#endif

#endif
