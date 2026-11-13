// Writer: our own language model, running on the board. Type the start of a story on the
// keyboard, tap WRITE, and the model carries on writing it, one letter at a time. Tap
// anywhere to stop it. (The model is trained on GitHub; see tools/train_writer.py.)
//
//   Screen:  a status line, the text (what you typed in white, what it wrote in yellow),
//            and a keyboard along the bottom.

#include "board.h"
#include "lcd.h"
#include "touch.h"
#include "lm.h"

#define COLS 40         // characters across (each 6 dots wide)
#define ROWS 23         // lines of text shown (each 8 dots tall)
#define TEXT_Y 14       // where the text starts
#define KEYS_Y 200      // where the keyboard starts
#define KEY 24          // each key is 24 x 24 dots
#define MAX_TEXT 4000
#define WRITE_LENGTH 400 // characters it writes each time you tap WRITE
#define TEMPERATURE 0.75f

#define BG       RGB(14, 16, 26)
#define TYPED    WHITE
#define WRITTEN  RGB(255, 214, 90)
#define KEY_BG   RGB(46, 52, 70)
#define KEY_DOWN RGB(110, 130, 190)
#define KEY_INK  WHITE
#define MUTED    RGB(140, 150, 170)

static int text_length(const char *s) { int n = 0; while (s[n]) n++; return n; }

// ---------- the text ----------

static char text[MAX_TEXT + 1];
static uint8_t written[MAX_TEXT + 1]; // 1 for characters the model wrote
static int length;

static void add(char c, int by_model) {
  if (length == MAX_TEXT) { // full: forget the oldest quarter
    int drop = MAX_TEXT / 4;
    for (int i = drop; i < length; i++) { text[i - drop] = text[i]; written[i - drop] = written[i]; }
    length -= drop;
  }
  text[length] = c;
  written[length] = (uint8_t)by_model;
  text[++length] = 0;
}

// ---------- showing the text ----------
// The screen is a grid of characters. After each change the text is laid out again (words
// wrapped onto the next line) and only the cells that changed are redrawn.

static char shown[ROWS][COLS];
static uint8_t shown_colour[ROWS][COLS];

static void draw_cell(int row, int col, char c, int by_model) {
  char s[2] = {c, 0};
  lcd_text(col * 6, TEXT_Y + row * 8, s, 1, by_model ? WRITTEN : TYPED, BG);
}

static void show_text(void) {
  static char grid[ROWS + 100][COLS];
  static uint8_t colour[ROWS + 100][COLS];
  const int capacity = ROWS + 100;
  // Lay out (only the end of the text can be on screen, so start a little before it).
  int from = length > COLS * (ROWS + 4) ? length - COLS * (ROWS + 4) : 0;
  while (from > 0 && text[from - 1] != ' ' && text[from - 1] != '\n') from++; // start at a word
  int row = 0, col = 0;
  for (int r = 0; r < capacity; r++) for (int c = 0; c < COLS; c++) { grid[r][c] = ' '; colour[r][c] = 0; }
  for (int i = from; i < length && row < capacity; i++) {
    char c = text[i];
    if (c == '\n') { row++; col = 0; continue; }
    if (c == ' ' && col == 0) continue;
    if (c != ' ' && col > 0 && (i == 0 || text[i - 1] == ' ')) { // a new word: will it fit?
      int w = 0;
      while (i + w < length && text[i + w] != ' ' && text[i + w] != '\n') w++;
      if (col + w > COLS && w <= COLS) { row++; col = 0; }
    }
    if (col == COLS) { row++; col = 0; }
    if (row >= capacity) break;
    grid[row][col] = c;
    colour[row][col] = written[i];
    col++;
  }
  int last = row < capacity ? row : capacity - 1; // the line being written on
  int top = last >= ROWS ? last - ROWS + 1 : 0;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      char ch = grid[top + r][c];
      uint8_t co = colour[top + r][c];
      if (ch != shown[r][c] || co != shown_colour[r][c]) {
        draw_cell(r, c, ch, co);
        shown[r][c] = ch;
        shown_colour[r][c] = co;
      }
    }
}

static void status(const char *s) {
  lcd_fill_rect(0, 0, LCD_W, 12, BG);
  lcd_text(2, 2, "WRITER", 1, MUTED, BG);
  lcd_text(LCD_W - 2 - 6 * text_length(s), 2, s, 1, WHITE, BG);
}

// ---------- the keyboard ----------

typedef struct { int x, y, w; const char *label; char ch; } Key;
#define DEL 1
#define CLEAR 2
#define WRITE 3
static Key keys[64];
static int n_keys;

static void add_row(int row, const char *chars) {
  for (int i = 0; chars[i]; i++) {
    Key k = {i * KEY, KEYS_Y + row * KEY, KEY, 0, chars[i]};
    keys[n_keys++] = k;
  }
}

static void keyboard_init(void) {
  add_row(0, "QWERTYUIOP");
  add_row(1, "ASDFGHJKL'");
  add_row(2, "ZXCVBNM,.?");
  add_row(3, "!-:;\"()/");
  Key del = {8 * KEY, KEYS_Y + 3 * KEY, 2 * KEY, "DEL", DEL};
  Key space = {0, KEYS_Y + 4 * KEY, 5 * KEY, "SPACE", ' '};
  Key clear = {5 * KEY, KEYS_Y + 4 * KEY, 2 * KEY, "CLEAR", CLEAR};
  Key write = {7 * KEY, KEYS_Y + 4 * KEY, 3 * KEY, "WRITE", WRITE};
  keys[n_keys++] = del; keys[n_keys++] = space; keys[n_keys++] = clear; keys[n_keys++] = write;
}

static void draw_key(const Key *k, int down) {
  uint16_t bg = down ? KEY_DOWN : (k->ch == WRITE ? RGB(40, 110, 70) : KEY_BG);
  lcd_fill_rect(k->x, k->y, k->w, KEY, BG);
  lcd_fill_rect(k->x + 1, k->y + 1, k->w - 2, KEY - 2, bg);
  if (k->label) {
    int len = text_length(k->label);
    lcd_text(k->x + (k->w - len * 6) / 2, k->y + 8, k->label, 1, KEY_INK, bg);
  } else {
    char s[2] = {k->ch, 0};
    lcd_text(k->x + 6, k->y + 4, s, 2, KEY_INK, bg);
  }
}

static int key_at(int x, int y) {
  for (int i = 0; i < n_keys; i++)
    if (x >= keys[i].x && x < keys[i].x + keys[i].w && y >= keys[i].y && y < keys[i].y + KEY) return i;
  return -1;
}

// ---------- writing ----------

enum { IDLE, READING, WRITING };
static int state = IDLE;
static int to_read_from, to_read_end; // the text the model still has to read before writing
static int written_now;
static uint32_t rng = 0x5EED1234u;

// Have the model read the end of the text (as much as it can remember, leaving room to write).
static void start_reading(void) {
  lm_reset();
  int keep = lm_memory() * 3 / 4;
  to_read_end = length;
  to_read_from = length > keep ? length - keep : 0;
  while (to_read_from < length && !lm_known(text[to_read_from])) to_read_from++;
  if (to_read_from == length) { lm_feed('\n'); } // nothing typed: start a fresh story
  state = READING;
  status("READING...");
}

static void stop(void) {
  if (state != IDLE) status("TAP WRITE");
  state = IDLE;
}

// One small piece of work: read a few characters, or write one.
static void work(void) {
  if (state == READING) {
    for (int n = 0; n < 4 && to_read_from < to_read_end; n++) lm_feed(text[to_read_from++]);
    if (to_read_from >= to_read_end) { state = WRITING; status("WRITING..."); }
    return;
  }
  if (state == WRITING) {
    if (written_now >= WRITE_LENGTH) { stop(); return; }
    if (lm_pos() >= lm_memory()) { start_reading(); return; } // memory full: re-read the latest part
    char c = lm_next(TEMPERATURE, &rng);
    add(c, 1);
    lm_feed(c);
    written_now++;
    show_text();
  }
}

int main(void) {
  board_fast_clock();
  leds_init();
  lcd_init();
  int has_touch = touch_init();
  lcd_fill_rect(0, 0, LCD_W, LCD_H, BG);
  for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) { shown[r][c] = ' '; shown_colour[r][c] = 0; }
  keyboard_init();
  for (int i = 0; i < n_keys; i++) draw_key(&keys[i], 0);

  if (!has_touch) status("NO TOUCH");
  else if (!lm_available()) status("NOT TRAINED YET");
  else status("TYPE, THEN WRITE");
  const char *hello = lm_available() ? "" : "I HAVE NOT LEARNED TO WRITE YET. MY TRAINING IS RUNNING ON GITHUB: FLASH ME AGAIN WHEN IT HAS FINISHED.";
  for (int i = 0; hello[i]; i++) add(hello[i], 1);
  show_text();

  int was_down = 0, pressed = -1;
  uint32_t beat = 0;
  for (;;) {
    int x = 0, y = 0;
    int down = has_touch && touch_read(&x, &y);
    if (down && !was_down) { // a new touch
      if (state != IDLE) { // anything stops the writing
        stop();
      } else {
        pressed = key_at(x, y);
        if (pressed >= 0) draw_key(&keys[pressed], 1);
      }
    }
    if (!down && was_down && pressed >= 0) { // a key was let go: do what it says
      char ch = keys[pressed].ch;
      draw_key(&keys[pressed], 0);
      if (ch == DEL) { if (length) text[--length] = 0; }
      else if (ch == CLEAR) { length = 0; text[0] = 0; }
      else if (ch == WRITE) {
        if (lm_available()) { written_now = 0; if (length && text[length - 1] != ' ' && text[length - 1] != '\n') add(' ', 0); start_reading(); }
      } else add(ch, 0);
      show_text();
      pressed = -1;
    }
    was_down = down;

    work();
    led_green((int)((++beat >> 7) & 1u)); // a slow heartbeat
    if (state == IDLE) delay_ms(10);
  }
}
