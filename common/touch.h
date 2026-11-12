// The touch layer on top of the screen.
#ifndef TOUCH_H_INCLUDED
#define TOUCH_H_INCLUDED

// Returns 1 if the touch chip answered, 0 if not (the program can carry on without touch).
int touch_init(void);

// Returns 1 if a finger is on the screen, and stores where (same coordinates as the
// screen: x 0..239 from the left, y 0..319 from the top). Returns 0 otherwise.
int touch_read(int *x, int *y);

#endif
