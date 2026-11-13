#include "fish_talk.h"
#include "talk_model.h"

#if TALK_FORMAT == 1

// ---------- maths the board has no library for ----------

static float absf(float v) { return v < 0.0f ? -v : v; }

// e to the power x, accurate to about one part in a million: split x into a whole number
// of doublings and a small leftover, and use a short polynomial for the leftover.
static float talk_exp(float x) {
  if (x < -80.0f) return 0.0f;
  if (x > 80.0f) x = 80.0f;
  float t = x * 1.44269504f; // x in powers of 2
  int whole = (int)(t < 0.0f ? t - 1.0f : t);
  float f = (t - (float)whole) * 0.69314718f; // leftover, back in powers of e: 0 .. 0.69
  float p = 1.0f + f * (1.0f + f * (0.5f + f * (0.16666667f + f * (0.04166667f + f * (0.00833333f + f * 0.00138889f)))));
  union { float f; uint32_t u; } two = {.u = (uint32_t)(whole + 127) << 23}; // 2^whole, built directly
  return p * two.f;
}

static float talk_tanh(float x) {
  if (x > 9.0f) return 1.0f;
  if (x < -9.0f) return -1.0f;
  float e = talk_exp(2.0f * x);
  return (e - 1.0f) / (e + 1.0f);
}

// ---------- the model ----------

#define HD (TALK_D / TALK_H) // width of each attention head

// What each character position has already worked out, kept so it needn't be worked out
// again for the next character (the "key" and "value" of each position, in each layer).
// Stored as whole numbers -127..127 with a scale, to fit in the board's memory.
#ifdef TALK_KV_FLOAT
static float kcache[TALK_L][TALK_CTX][TALK_D], vcache[TALK_L][TALK_CTX][TALK_D];
#else
static int8_t kcache[TALK_L][TALK_CTX][TALK_D], vcache[TALK_L][TALK_CTX][TALK_D];
static float kscale[TALK_L][TALK_CTX], vscale[TALK_L][TALK_CTX];
#endif

static float scores[TALK_V]; // how likely each character is to come next

// The voice of whoever is speaking (0 = the model's own, plain voice). Its accent genes nudge
// the model's last step, so each family's way of talking drifts over the generations.
static const Genome *voice;

// A fixed, made-up direction in the model's thinking for each accent gene, +1 or -1 for each
// of its numbers (the same everywhere, so an accent means the same thing on every device).
static float accent_sign(int i, int k) { return ((((uint32_t)(i * 31 + k * 17 + 7) * 2654435761u) >> 13) & 1u) ? 0.125f : -0.125f; }

// out = weights (rows of whole numbers, each row with its own scale) times in
static void matvec(float *out, const int8_t *w, const float *scale, const float *in, int rows, int cols) {
  for (int r = 0; r < rows; r++) {
    const int8_t *row = w + r * cols;
    float sum = 0.0f;
    for (int c = 0; c < cols; c++) sum += (float)row[c] * in[c];
    out[r] = sum * scale[r];
  }
}

static void layer_norm(float *out, const float *in, const float *g, const float *b) {
  float mean = 0.0f, var = 0.0f;
  for (int i = 0; i < TALK_D; i++) mean += in[i];
  mean /= TALK_D;
  for (int i = 0; i < TALK_D; i++) var += (in[i] - mean) * (in[i] - mean);
  float inv = 1.0f / __builtin_sqrtf(var / TALK_D + 1e-5f);
  for (int i = 0; i < TALK_D; i++) out[i] = (in[i] - mean) * inv * g[i] + b[i];
}

#ifndef TALK_KV_FLOAT
static void store(int8_t *dst, float *scale, const float *src) {
  float most = 0.0f;
  for (int i = 0; i < TALK_D; i++) if (absf(src[i]) > most) most = absf(src[i]);
  float s = most > 0.0f ? most / 127.0f : 1.0f, inv = 1.0f / s;
  for (int i = 0; i < TALK_D; i++) {
    float v = src[i] * inv;
    dst[i] = (int8_t)(v < 0.0f ? v - 0.5f : v + 0.5f);
  }
  *scale = s;
}
#endif

// Feed one character (at position `pos`) through the model; leaves the scores for the next.
static void forward(int token, int pos) {
  static float x[TALK_D], h[TALK_D], qkv[3 * TALK_D], att[TALK_D], tmp[TALK_D], ff[TALK_FF], weights[TALK_CTX];
  for (int i = 0; i < TALK_D; i++)
    x[i] = (float)TALK_TOK[token * TALK_D + i] * TALK_TOK_S[token] + (float)TALK_POS[pos * TALK_D + i] * TALK_POS_S[pos];

  for (int l = 0; l < TALK_L; l++) {
    // Attention: this position looks back over every earlier one.
    layer_norm(h, x, TALK_LN1_G + l * TALK_D, TALK_LN1_B + l * TALK_D);
    matvec(qkv, TALK_QKV + l * 3 * TALK_D * TALK_D, TALK_QKV_S + l * 3 * TALK_D, h, 3 * TALK_D, TALK_D);
    const float *q = qkv, *k = qkv + TALK_D, *v = qkv + 2 * TALK_D;
#ifdef TALK_KV_FLOAT
    for (int i = 0; i < TALK_D; i++) { kcache[l][pos][i] = k[i]; vcache[l][pos][i] = v[i]; }
#else
    store(kcache[l][pos], &kscale[l][pos], k);
    store(vcache[l][pos], &vscale[l][pos], v);
#endif
    const float scale = 1.0f / __builtin_sqrtf((float)HD);
    for (int hh = 0; hh < TALK_H; hh++) {
      const int o = hh * HD;
      float most = -1e30f;
      for (int p = 0; p <= pos; p++) {
        float dot = 0.0f;
        for (int i = 0; i < HD; i++) dot += q[o + i] * (float)kcache[l][p][o + i];
#ifndef TALK_KV_FLOAT
        dot *= kscale[l][p];
#endif
        weights[p] = dot * scale;
        if (weights[p] > most) most = weights[p];
      }
      float total = 0.0f;
      for (int p = 0; p <= pos; p++) { weights[p] = talk_exp(weights[p] - most); total += weights[p]; }
      for (int i = 0; i < HD; i++) att[o + i] = 0.0f;
      for (int p = 0; p <= pos; p++) {
        float w = weights[p] / total;
#ifndef TALK_KV_FLOAT
        w *= vscale[l][p];
#endif
        for (int i = 0; i < HD; i++) att[o + i] += w * (float)vcache[l][p][o + i];
      }
    }
    matvec(tmp, TALK_PROJ + l * TALK_D * TALK_D, TALK_PROJ_S + l * TALK_D, att, TALK_D, TALK_D);
    for (int i = 0; i < TALK_D; i++) x[i] += tmp[i];

    // A small neural network, the same for every position.
    layer_norm(h, x, TALK_LN2_G + l * TALK_D, TALK_LN2_B + l * TALK_D);
    matvec(ff, TALK_FC + l * TALK_FF * TALK_D, TALK_FC_S + l * TALK_FF, h, TALK_FF, TALK_D);
    for (int i = 0; i < TALK_FF; i++) { // GELU, the "tanh" version, as in the training
      float u = ff[i];
      ff[i] = 0.5f * u * (1.0f + talk_tanh(0.7978845608f * (u + 0.044715f * u * u * u)));
    }
    matvec(tmp, TALK_OUT + l * TALK_D * TALK_FF, TALK_OUT_S + l * TALK_D, ff, TALK_D, TALK_FF);
    for (int i = 0; i < TALK_D; i++) x[i] += tmp[i];
  }
  layer_norm(h, x, TALK_LNF_G, TALK_LNF_B);
  if (voice)
    for (int i = 0; i < TALK_D; i++) {
      float lean = 0.0f;
      for (int k = 0; k < VOICE_ACCENT; k++) lean += accent_sign(i, k) * voice->accent[k];
      h[i] += 0.6f * lean;
    }
  matvec(scores, TALK_TOK, TALK_TOK_S, h, TALK_V, TALK_D); // the character table, used backwards
}

static const char VOCAB[] = TALK_VOCAB;

static int token_of(char c) {
  for (int i = 0; i < TALK_V; i++) if (VOCAB[i] == c) return i;
  return 1; // (a space)
}

// ---------- picking characters ----------

static uint32_t talk_rng = 1;
static float rnd(void) { return (float)(rng_next(&talk_rng) >> 8) * (1.0f / 16777216.0f); }

static int pick(float temperature) {
  float most = -1e30f, total = 0.0f;
  for (int i = 0; i < TALK_V; i++) if (scores[i] > most) most = scores[i];
  static float p[TALK_V];
  for (int i = 0; i < TALK_V; i++) { p[i] = talk_exp((scores[i] - most) / temperature); total += p[i]; }
  float r = rnd() * total;
  for (int i = 0; i < TALK_V; i++) { r -= p[i]; if (r <= 0.0f) return i; }
  return TALK_V - 1;
}

// ---------- what gets talked about ----------

// The situations, as the model was taught them. Each opening line (even number) is
// answered with the one after it; after that, a conversation may carry on as CHAT.
enum { T_BITE, T_BITTEN, T_FIGHT, T_FOUGHT, T_BORN, T_NEWBORN, T_LIKE, T_LIKED, T_HATE, T_HATED,
       T_EVOLVE, T_EVOLVED, T_MOURN, T_GLOAT, T_GREET, T_GREETED, T_HUNGRY, T_BEGGED, T_CHAT };
static const char *const SITUATION[] = {"BITE", "BITTEN", "FIGHT", "FOUGHT", "BORN", "NEWBORN", "LIKE", "LIKED",
  "HATE", "HATED", "EVOLVE", "EVOLVED", "MOURN", "GLOAT", "GREET", "GREETED", "HUNGRY", "BEGGED", "CHAT"};
#define LONGEST 4 // lines in one conversation
static const char *const KIND_WORD[N_KINDS] = {"FRY", "SQUID", "TURTLE", "SHARK", "RAY", "ORCA", "WHALE"};

typedef struct {
  uint8_t situation, speaker, listener, turn; // turn: 0 the opening line, 1 the reply, ...
  uint16_t speaker_id, listener_id;
  char said[TALK_LINE + 1]; // what the other one said (for a reply)
} Request;

#define QUEUE 4
static Request queue[QUEUE];
static int queued;
static char names[N_FISH][4];
static int quiet;
static uint32_t noticed_step = 0xFFFFFFFFu;
static int noticed_events, next_chat;

static struct {
  int active;
  Request req;
  char prompt[TALK_CTX];
  int length, fed; // prompt length, and how much of it the model has read
  char out[TALK_LINE + 1];
  int written;
} now;

#define DONE 6
static TalkLine done[DONE];
static int done_first, done_count;

static void ask_turn(int situation, int speaker, int listener, const Tank *t, const char *said, int urgent, int turn) {
  if (speaker == listener || speaker >= N_FISH || listener >= N_FISH) return;
  if (queued == QUEUE) { if (!urgent) return; queued--; } // full: an urgent one pushes out the last
  Request r = {(uint8_t)situation, (uint8_t)speaker, (uint8_t)listener, (uint8_t)turn, t->fish[speaker].id, t->fish[listener].id, {0}};
  if (said) { int i = 0; for (; said[i] && i < TALK_LINE; i++) r.said[i] = said[i]; r.said[i] = 0; }
  if (urgent) { // to the front of the queue
    for (int i = queued; i > 0; i--) queue[i] = queue[i - 1];
    queue[0] = r;
  } else {
    queue[queued] = r;
  }
  queued++;
}

static void ask(int situation, int speaker, int listener, const Tank *t, const char *said, int urgent) {
  ask_turn(situation, speaker, listener, t, said, urgent, 0);
}

static int alive(const Tank *t, int i, uint16_t id) { return !t->fish[i].dead && t->fish[i].id == id; }

// The two personality traits that stand out most (or EVEN).
static void trait_words(const Fish *f, const char *out[2]) {
  static const char *const HIGH[5] = {"BOLD", "SOCIAL", "ANGRY", "LOYAL", "CURIOUS"};
  static const char *const LOW[5] = {"TIMID", "LONER", "CALM", "FICKLE", "CAUTIOUS"};
  const float v[5] = {f->g.boldness, f->g.sociability, f->g.aggression, f->g.loyalty, f->g.curiosity};
  int first = -1, second = -1;
  for (int k = 0; k < 5; k++) {
    float strength = absf(v[k] - 0.5f);
    if (strength <= 0.2f) continue;
    if (first < 0 || strength > absf(v[first] - 0.5f)) { second = first; first = k; }
    else if (second < 0 || strength > absf(v[second] - 0.5f)) second = k;
  }
  out[0] = first < 0 ? "EVEN" : v[first] > 0.5f ? HIGH[first] : LOW[first];
  out[1] = second < 0 ? "EVEN" : v[second] > 0.5f ? HIGH[second] : LOW[second];
}

static const char *feeling_word(const Tank *t, int a, int b) {
  if (t->like[a][b] < -0.3f) return "ENEMY";
  if (t->fish[a].family == t->fish[b].family) return "FAMILY";
  if (t->like[a][b] > 0.3f) return "FRIEND";
  return "STRANGER";
}

static int append(char *buf, int n, const char *text) {
  while (*text && n < TALK_CTX - TALK_LINE - 2) buf[n++] = *text++;
  return n;
}

// "BITE SHARK BOLD ANGRY STRANGER ADA: " (and " SAID ..." before the colon for a reply)
static void build_prompt(const Tank *t, const Request *r) {
  const Fish *f = &t->fish[r->speaker];
  const char *tr[2];
  trait_words(f, tr);
  char name[4] = {names[r->listener][0], names[r->listener][1], names[r->listener][2], 0};
  const char *parts[] = {SITUATION[r->situation], " ", KIND_WORD[f->kind], " ", tr[0], " ", tr[1], " ",
                         feeling_word(t, r->speaker, r->listener), " ", name};
  int n = 0;
  for (unsigned k = 0; k < sizeof parts / sizeof parts[0]; k++) n = append(now.prompt, n, parts[k]);
  if (r->said[0]) { n = append(now.prompt, n, " SAID "); n = append(now.prompt, n, r->said); }
  n = append(now.prompt, n, ": ");
  now.length = n;
}

int talk_available(void) { return 1; }

void talk_init(uint32_t seed) {
  talk_rng = seed | 1u;
  queued = 0;
  now.active = 0;
  done_count = 0;
  next_chat = 120;
  noticed_step = 0xFFFFFFFFu;
  for (int i = 0; i < N_FISH; i++) { names[i][0] = names[i][1] = names[i][2] = '?'; names[i][3] = 0; }
}

void talk_set_name(int i, const char *name) {
  if (i < 0 || i >= N_FISH) return;
  for (int c = 0; c < 3; c++) {
    char ch = name[c] >= 'a' && name[c] <= 'z' ? (char)(name[c] - 32) : name[c];
    names[i][c] = (ch >= 'A' && ch <= 'Z') ? ch : '?';
  }
}

void talk_quiet(int on) { quiet = on; if (on) { queued = 0; now.active = 0; } }

void talk_notice(const Tank *t) {
  if (t->step != noticed_step) { noticed_step = t->step; noticed_events = 0; }
  if (quiet) { noticed_events = t->n_events; return; }

  for (; noticed_events < t->n_events; noticed_events++) {
    const Event *e = &t->events[noticed_events];
    int a = e->a, b = e->b;
    switch (e->type) {
      case EV_BIT:   if (rnd() < 0.5f) ask(T_BITE, a, b, t, 0, 0); break;
      case EV_FOUGHT: ask(T_FIGHT, a, b, t, 0, 0); break;
      case EV_BORN:  ask(T_BORN, b, a, t, 0, 1); break; // the mum welcomes the baby
      case EV_LIKES: if (rnd() < 0.4f) ask(T_LIKE, a, b, t, 0, 0); break;
      case EV_HATES: if (rnd() < 0.6f) ask(T_HATE, a, b, t, 0, 0); break;
      case EV_EVOLVED: { // shows off to its best friend, or whoever is closest
        int to = -1;
        float best = 0.2f, nearest = 1e9f;
        for (int j = 0; j < N_FISH; j++) if (j != a && !t->fish[j].dead && t->like[a][j] > best) { best = t->like[a][j]; to = j; }
        if (to < 0)
          for (int j = 0; j < N_FISH; j++) {
            if (j == a || t->fish[j].dead) continue;
            float dx = t->fish[j].x - t->fish[a].x, dy = t->fish[j].y - t->fish[a].y;
            if (dx * dx + dy * dy < nearest) { nearest = dx * dx + dy * dy; to = j; }
          }
        if (to >= 0) ask(T_EVOLVE, a, to, t, 0, 0);
        break;
      }
      case EV_ATE: { // whoever loved the one eaten has words for the eater
        int mourner = -1;
        float best = 0.25f;
        for (int j = 0; j < N_FISH; j++) {
          if (j == a || j == b || t->fish[j].dead) continue;
          float love = t->like[j][b] + (t->fish[j].family == t->fish[b].family ? 0.3f : 0.0f);
          if (love > best) { best = love; mourner = j; }
        }
        if (mourner >= 0) ask(T_MOURN, mourner, a, t, 0, 1);
        break;
      }
    }
  }

  // Now and then, when nothing else is being said, two animals that are near each other chat.
  if (--next_chat <= 0 && queued == 0 && !now.active) {
    next_chat = 150 + (int)(rnd() * 150.0f);
    int a = (int)(rnd() * N_FISH) % N_FISH, b = -1;
    float nearest = 70.0f * 70.0f;
    for (int j = 0; j < N_FISH; j++) {
      if (j == a || t->fish[j].dead) continue;
      float dx = t->fish[j].x - t->fish[a].x, dy = t->fish[j].y - t->fish[a].y;
      if (dx * dx + dy * dy < nearest) { nearest = dx * dx + dy * dy; b = j; }
    }
    if (b >= 0 && !t->fish[a].dead && rnd() < 0.25f + 0.75f * t->fish[a].g.chattiness) // chatty ones start more
      ask(t->fish[a].energy < 0.3f ? T_HUNGRY : T_GREET, a, b, t, 0, 0);
  }
}

static void finish(const Tank *t) {
  voice = 0;
  int n = now.written;
  while (n > 0 && now.out[n - 1] == ' ') n--;
  now.out[n] = 0;
  now.active = 0;
  if (!n) return;
  TalkLine *line = &done[(done_first + done_count) % DONE];
  if (done_count == DONE) done_first = (done_first + 1) % DONE; else done_count++;
  line->speaker = now.req.speaker;
  line->listener = now.req.listener;
  for (int i = 0; i <= n; i++) line->text[i] = now.out[i];
  // The other one answers, having heard it; after the reply, they may carry on (friends and
  // enemies have more to say to each other than strangers do).
  if (!alive(t, now.req.listener, now.req.listener_id) || !alive(t, now.req.speaker, now.req.speaker_id)) return;
  int turn = now.req.turn;
  if (turn == 0) {
    ask_turn(now.req.situation + 1, now.req.listener, now.req.speaker, t, now.out, 1, 1);
  } else if (turn + 1 < LONGEST) {
    const char *f = feeling_word(t, now.req.listener, now.req.speaker);
    float chance = (f[0] == 'S' ? 0.35f : 0.6f) * (0.5f + t->fish[now.req.listener].g.chattiness); // (S: STRANGER)
    if (rnd() < chance) ask_turn(T_CHAT, now.req.listener, now.req.speaker, t, now.out, 1, turn + 1);
  }
}

void talk_work(const Tank *t, int chars) {
  while (chars > 0) {
    if (!now.active) {
      if (!queued) return;
      now.req = queue[0];
      for (int i = 1; i < queued; i++) queue[i - 1] = queue[i];
      queued--;
      // It may have died (or been replaced by a baby) while waiting its turn.
      if (!alive(t, now.req.speaker, now.req.speaker_id) || !alive(t, now.req.listener, now.req.listener_id)) continue;
      build_prompt(t, &now.req);
      now.fed = now.written = 0;
      now.active = 1;
      voice = &t->fish[now.req.speaker].g;
    }
    if (now.fed < now.length) { // reading what's going on
      forward(token_of(now.prompt[now.fed]), now.fed);
      now.fed++;
      chars--;
      continue;
    }
    // Writing, in the speaker's voice: its creativity sets how adventurous the choices are,
    // and it leans towards "!", "?" or ending the line according to its genes.
    const Genome *g = voice;
    if (g) {
      scores[token_of('!')] += 2.5f * (g->exclaim - 0.5f);
      scores[token_of('?')] += 2.5f * (g->question - 0.5f);
      if (now.written >= 6) scores[0] += 2.5f * (g->brevity - 0.5f); // (0 is the end of a line)
    }
    int c = pick(g ? 0.55f + 0.35f * g->creativity : 0.7f);
    if (VOCAB[c] == '\n' || now.written == TALK_LINE || now.length + now.written >= TALK_CTX - 1) { finish(t); continue; }
    now.out[now.written++] = VOCAB[c];
    forward(c, now.length + now.written - 1);
    chars--;
  }
}

int talk_take(TalkLine *line) {
  if (!done_count) return 0;
  *line = done[done_first];
  done_first = (done_first + 1) % DONE;
  done_count--;
  return 1;
}

int talk_current(int *speaker, int *listener, char *so_far) {
  if (!now.active) return 0;
  *speaker = now.req.speaker;
  *listener = now.req.listener;
  for (int i = 0; i < now.written; i++) so_far[i] = now.out[i];
  so_far[now.written] = 0;
  return 1;
}

#ifdef TALK_TOOLS
int talk_score(const char *prompt, float *out) {
  voice = 0;
  int n = 0;
  for (; prompt[n]; n++) forward(token_of(prompt[n]), n);
  for (int i = 0; i < TALK_V; i++) out[i] = scores[i];
  return TALK_V;
}

void talk_say(const char *prompt, char *out, uint32_t seed) {
  talk_rng = seed | 1u;
  voice = 0;
  int n = 0, w = 0;
  for (; prompt[n]; n++) forward(token_of(prompt[n]), n);
  while (w < TALK_LINE) {
    int c = pick(0.7f);
    if (VOCAB[c] == '\n') break;
    out[w++] = VOCAB[c];
    forward(c, n + w - 1);
  }
  out[w] = 0;
}
#endif

#else // no talk model trained yet: the animals stay quiet

int talk_available(void) { return 0; }
void talk_init(uint32_t seed) { (void)seed; }
void talk_set_name(int i, const char *name) { (void)i; (void)name; }
void talk_quiet(int on) { (void)on; }
void talk_notice(const Tank *tank) { (void)tank; }
void talk_work(const Tank *tank, int chars) { (void)tank; (void)chars; }
int talk_take(TalkLine *line) { (void)line; return 0; }
int talk_current(int *speaker, int *listener, char *so_far) { (void)speaker; (void)listener; (void)so_far; return 0; }

#endif
