#include "fish_view.h"

void fish_view(const Tank *tank, unsigned born, float out[VIEW_SIZE]) {
  float *p = out;
  *p++ = (float)tank->step;
  *p++ = (float)tank->total_eaten;
  *p++ = (float)tank->total_killed;
  *p++ = (float)tank->total_starved;
  *p++ = (float)born;
  *p++ = tank->nutrients;
  *p++ = 0.0f;
  *p++ = 0.0f;
  for (int i = 0; i < N_FISH; i++) {
    const Fish *f = &tank->fish[i];
    *p++ = f->x; *p++ = f->y; *p++ = f->dx; *p++ = f->dy;
    *p++ = f->size; *p++ = f->slim; *p++ = f->energy; *p++ = f->health;
    *p++ = f->score; *p++ = (float)f->age; *p++ = (float)f->kills; *p++ = (float)f->eaten;
    *p++ = (float)f->stage; *p++ = f->growth;
  }
  for (int k = 0; k < N_FOOD; k++) {
    *p++ = tank->food[k].x; *p++ = tank->food[k].y; *p++ = (float)tank->food[k].there;
  }
  for (int i = 0; i < N_FISH; i++)
    for (int j = 0; j < N_FISH; j++) *p++ = tank->like[i][j];
  for (int k = 0; k < N_PLANTS; k++) {
    *p++ = tank->plants[k].x; *p++ = tank->plants[k].height;
  }
}
