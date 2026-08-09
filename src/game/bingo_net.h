#ifndef BINGO_NET_H
#define BINGO_NET_H

#include "macros.h"
#include "types.h"
#include "bingo.h"

// Online bingo game hooks. All of these are no-ops on the N64 build and
// when no --net-server was given on the PC build.

// Per-frame: keep ghost Mario puppets in sync and apply server claims.
void bingo_net_update(void);

// A cell completed through local gameplay; forwards it to the server.
void bingo_net_on_local_complete(struct BingoObjective *objective);

// Behavior update for the ghost puppet object (bhvNetGhost).
void bhv_net_ghost_update(void);

// True if obj is a ghost puppet. Used by Mario's geo ASM functions so
// puppets don't mirror the local player's body state (always 0 on N64).
s32 bingo_net_obj_is_ghost(struct Object *obj);

#endif // BINGO_NET_H
