#ifndef FONT_H_INCLUDED
#define FONT_H_INCLUDED

#define FONT_COLS 5 // dots across
#define FONT_ROWS 7 // dots down

// Returns the picture of a character as 35 characters ('X' = lit), row by row,
// or 0 if the character isn't in the font.
const char *font_glyph(char c);

#endif
