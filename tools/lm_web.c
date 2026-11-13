// The bridge between the Writer's language model (Writer/lm.c) and the website. GitHub
// compiles this into writer.wasm, so the page runs the very same model as the board.
#include "lm.h"

#define EXPORT __attribute__((visibility("default")))

void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  while (n--) *d++ = *s++;
  return dst;
}
void *memset(void *dst, int value, __SIZE_TYPE__ n) {
  unsigned char *d = dst;
  while (n--) *d++ = (unsigned char)value;
  return dst;
}

static uint32_t rng = 0x5EED1234u;
EXPORT int wr_available(void) { return lm_available(); }
EXPORT int wr_memory(void) { return lm_memory(); }
EXPORT void wr_reset(void) { lm_reset(); }
EXPORT int wr_known(int c) { return lm_known((char)c); }
EXPORT void wr_feed(int c) { lm_feed((char)c); }
EXPORT int wr_pos(void) { return lm_pos(); }
EXPORT void wr_seed(unsigned s) { rng = s | 1u; }
EXPORT int wr_next(float temperature) { return lm_next(temperature, &rng); }
