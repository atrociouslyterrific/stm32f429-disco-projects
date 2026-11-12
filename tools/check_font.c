// Runs on GitHub during the build (not on the board): checks every letter picture in
// common/font.c is exactly 5x7, and prints a sample so mistakes are visible in the build log.

#include <stdio.h>
#include <string.h>
#include "font.h"

int main(void) {
  const char *all = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?:.-+";
  int bad = 0;
  for (const char *p = all; *p; p++) {
    const char *g = font_glyph(*p);
    if (!g) { printf("missing: %c\n", *p); bad = 1; continue; }
    if (strlen(g) != FONT_COLS * FONT_ROWS) { printf("wrong size: %c (%zu dots)\n", *p, strlen(g)); bad = 1; }
    for (const char *q = g; *q; q++)
      if (*q != 'X' && *q != '.') { printf("odd dot '%c' in: %c\n", *q, *p); bad = 1; }
  }
  if (bad) return 1;

  const char *lines[] = {"HELLO! STM32F429", "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789 !?:.-+"};
  for (unsigned l = 0; l < sizeof lines / sizeof lines[0]; l++) {
    for (int row = 0; row < FONT_ROWS; row++) {
      for (const char *p = lines[l]; *p; p++) {
        const char *g = font_glyph(*p);
        for (int col = 0; col < FONT_COLS; col++) putchar(g && g[row * FONT_COLS + col] == 'X' ? '#' : ' ');
        putchar(' ');
      }
      putchar('\n');
    }
    putchar('\n');
  }
  return 0;
}
