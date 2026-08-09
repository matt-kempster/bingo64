#ifndef SPLATOON_H
#define SPLATOON_H

#include <ultra64.h>
#include "types.h"

// Splatoon mode: Mario paints the floor triangles he steps on.
// Off by default; tests (and later the modifier system) turn it on.
extern s32 gSplatoonEnabled;
extern s32 gSplatoonPaintedCount;
extern s32 gSplatoonTotalFloors;

void splatoon_static_surfaces_reset(void);
void splatoon_register_static_surface(struct Surface *surf);
void splatoon_clear(void);
void splatoon_on_area_load(s16 areaIndex);
void splatoon_render(void);

#endif // SPLATOON_H
