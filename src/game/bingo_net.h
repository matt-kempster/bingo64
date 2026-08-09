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

#endif // BINGO_NET_H
