#ifndef _BINGO_CRUSHERS_H
#define _BINGO_CRUSHERS_H

#include <ultra64.h>
#include "types.h"

// Registers a crusher in the crusher UID table, keyed on the given position.
// Idempotent: safe to call from a behavior loop every frame. Called from each
// whitelisted crusher's init.
void bingo_register_crusher(struct Object *obj, f32 keyX, f32 keyY, f32 keyZ);

// Credits "crushed by a distinct crusher" if the ceiling that is squishing
// Mario belongs to a whitelisted crusher. Called immediately before each of
// the four transitions into ACT_SQUISHED.
void bingo_track_crushed(struct MarioState *m);

#endif /* _BINGO_CRUSHERS_H */
