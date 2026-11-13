#include "lm.h"
#include "lm_model.h"

#if LM_FORMAT == 1

static float absf(float v) { return v < 0.0f ? -v : v; }

// e to the power x, accurate to about one part in a million (no maths library on the board).
static float lm_exp(float x) {
  if (x < -80.0f) return 0.0f;
  if (x > 80.0f) x = 80.0f;
  float t = x * 1.44269504f;
  int whole = (int)(t < 0.0f ? t - 1.0f : t);
  float f = (t - (float)whole) * 0.69314718f;
  float p = 1.0f + f * (1.0f + f * (0.5f + f * (0.16666667f + f * (0.04166667f + f * (0.00833333f + f * 0.00138889f)))));
  union { float f; uint32_t u; } two = {.u = (uint32_t)(whole + 127) << 23};
  return p * two.f;
}

static float lm_tanh(float x) {
  if (x > 9.0f) return 1.0f;
  if (x < -9.0f) return -1.0f;
  float e = lm_exp(2.0f * x);
  return (e - 1.0f) / (e + 1.0f);
}

#define HD (LM_D / LM_H)

// What each earlier character worked out ("keys" and "values"), kept so it needn't be
// worked out again. Rounded to whole numbers with a scale each, to fit the board's memory.
#ifdef LM_KV_FLOAT
static float kcache[LM_L][LM_CTX][LM_D], vcache[LM_L][LM_CTX][LM_D];
#else
static int8_t kcache[LM_L][LM_CTX][LM_D], vcache[LM_L][LM_CTX][LM_D];
static float kscale[LM_L][LM_CTX], vscale[LM_L][LM_CTX];
#endif
static float scores[LM_V];
static int position;
static const char VOCAB[] = LM_VOCAB;

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
  for (int i = 0; i < LM_D; i++) mean += in[i];
  mean /= LM_D;
  for (int i = 0; i < LM_D; i++) var += (in[i] - mean) * (in[i] - mean);
  float inv = 1.0f / __builtin_sqrtf(var / LM_D + 1e-5f);
  for (int i = 0; i < LM_D; i++) out[i] = (in[i] - mean) * inv * g[i] + b[i];
}

#ifndef LM_KV_FLOAT
static void store(int8_t *dst, float *scale, const float *src) {
  float most = 0.0f;
  for (int i = 0; i < LM_D; i++) if (absf(src[i]) > most) most = absf(src[i]);
  float s = most > 0.0f ? most / 127.0f : 1.0f, inv = 1.0f / s;
  for (int i = 0; i < LM_D; i++) { float v = src[i] * inv; dst[i] = (int8_t)(v < 0.0f ? v - 0.5f : v + 0.5f); }
  *scale = s;
}
#endif

static void forward(int token, int pos) {
  static float x[LM_D], h[LM_D], qkv[3 * LM_D], att[LM_D], tmp[LM_D], ff[LM_FF], weights[LM_CTX];
  for (int i = 0; i < LM_D; i++)
    x[i] = (float)LM_TOK[token * LM_D + i] * LM_TOK_S[token] + (float)LM_POS[pos * LM_D + i] * LM_POS_S[pos];
  for (int l = 0; l < LM_L; l++) {
    layer_norm(h, x, LM_LN1_G + l * LM_D, LM_LN1_B + l * LM_D);
    matvec(qkv, LM_QKV + l * 3 * LM_D * LM_D, LM_QKV_S + l * 3 * LM_D, h, 3 * LM_D, LM_D);
    const float *q = qkv, *k = qkv + LM_D, *v = qkv + 2 * LM_D;
#ifdef LM_KV_FLOAT
    for (int i = 0; i < LM_D; i++) { kcache[l][pos][i] = k[i]; vcache[l][pos][i] = v[i]; }
#else
    store(kcache[l][pos], &kscale[l][pos], k);
    store(vcache[l][pos], &vscale[l][pos], v);
#endif
    const float scale = 1.0f / __builtin_sqrtf((float)HD);
    for (int hh = 0; hh < LM_H; hh++) {
      const int o = hh * HD;
      float most = -1e30f;
      for (int p = 0; p <= pos; p++) {
        float dot = 0.0f;
        for (int i = 0; i < HD; i++) dot += q[o + i] * (float)kcache[l][p][o + i];
#ifndef LM_KV_FLOAT
        dot *= kscale[l][p];
#endif
        weights[p] = dot * scale;
        if (weights[p] > most) most = weights[p];
      }
      float total = 0.0f;
      for (int p = 0; p <= pos; p++) { weights[p] = lm_exp(weights[p] - most); total += weights[p]; }
      for (int i = 0; i < HD; i++) att[o + i] = 0.0f;
      for (int p = 0; p <= pos; p++) {
        float w = weights[p] / total;
#ifndef LM_KV_FLOAT
        w *= vscale[l][p];
#endif
        for (int i = 0; i < HD; i++) att[o + i] += w * (float)vcache[l][p][o + i];
      }
    }
    matvec(tmp, LM_PROJ + l * LM_D * LM_D, LM_PROJ_S + l * LM_D, att, LM_D, LM_D);
    for (int i = 0; i < LM_D; i++) x[i] += tmp[i];
    layer_norm(h, x, LM_LN2_G + l * LM_D, LM_LN2_B + l * LM_D);
    matvec(ff, LM_FC + l * LM_FF * LM_D, LM_FC_S + l * LM_FF, h, LM_FF, LM_D);
    for (int i = 0; i < LM_FF; i++) { float u = ff[i]; ff[i] = 0.5f * u * (1.0f + lm_tanh(0.7978845608f * (u + 0.044715f * u * u * u))); }
    matvec(tmp, LM_OUT + l * LM_D * LM_FF, LM_OUT_S + l * LM_D, ff, LM_D, LM_FF);
    for (int i = 0; i < LM_D; i++) x[i] += tmp[i];
  }
  layer_norm(h, x, LM_LNF_G, LM_LNF_B);
  matvec(scores, LM_TOK, LM_TOK_S, h, LM_V, LM_D);
}

static int token_of(char c) {
  for (int i = 0; i < LM_V; i++) if (VOCAB[i] == c) return i;
  return -1;
}

int lm_available(void) { return 1; }
int lm_memory(void) { return LM_CTX; }
void lm_reset(void) { position = 0; }
int lm_known(char c) { return token_of(c) >= 0; }
int lm_pos(void) { return position; }

void lm_feed(char c) {
  int t = token_of(c);
  if (t < 0 || position >= LM_CTX) return;
  forward(t, position);
  position++;
}

char lm_next(float temperature, uint32_t *rng) {
  if (!position) return ' ';
  float most = -1e30f, total = 0.0f;
  static float p[LM_V];
  for (int i = 0; i < LM_V; i++) if (scores[i] > most) most = scores[i];
  for (int i = 0; i < LM_V; i++) { p[i] = lm_exp((scores[i] - most) / temperature); total += p[i]; }
  uint32_t x = *rng;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  *rng = x;
  float r = (float)(x >> 8) * (1.0f / 16777216.0f) * total;
  for (int i = 0; i < LM_V; i++) { r -= p[i]; if (r <= 0.0f) return VOCAB[i]; }
  return VOCAB[LM_V - 1];
}

#ifdef LM_TOOLS
int lm_score(const char *prompt, float *out) {
  lm_reset();
  for (int i = 0; prompt[i]; i++) lm_feed(prompt[i]);
  for (int i = 0; i < LM_V; i++) out[i] = scores[i];
  return LM_V;
}
#endif

#else // not trained yet

int lm_available(void) { return 0; }
int lm_memory(void) { return 0; }
void lm_reset(void) {}
int lm_known(char c) { (void)c; return 0; }
void lm_feed(char c) { (void)c; }
int lm_pos(void) { return 0; }
char lm_next(float temperature, uint32_t *rng) { (void)temperature; (void)rng; return ' '; }

#endif
