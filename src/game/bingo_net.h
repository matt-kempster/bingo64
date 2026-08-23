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

// Online race status for the shared win logic in bingo.c. On N64 (and
// offline) these report "not online": 0 / 0 / 0.
s32 bingo_net_racing(void);           // in an online race right now
s32 bingo_net_local_cell_count(void); // cells the local player owns online
s32 bingo_net_bingo_count(s32 claimerId); // complete lines a peer holds
s32 bingo_net_race_decided(void);     // an online winner exists (lockout)
s32 bingo_net_local_won(void);        // ...and it is the local player

// The room went back to the lobby: clear the local race state so the
// next race sets up from scratch (called by the PC network client).
void bingo_net_on_room_reset(void);
// Level-update poll, 1 exactly once after a room reset: leave the level
// and warp back to the file select lobby. Always 0 on N64 / offline.
s32 bingo_net_take_lobby_return(void);
void bingo_net_request_menu_return(void);
void bingo_net_keepalive(void);

#ifndef TARGET_N64
// The palette index of the peer driving this ghost puppet (0 = red).
s32 bingo_net_ghost_color(struct Object *obj);

// A copy of `dl` with Mario's red-material lights swapped for the given
// palette color, so each ghost wears its owner's hat color. Clones are
// built lazily and cached; returns `dl` itself for color 0 (red) or when
// the display list touches no red material.
void *bingo_net_tinted_dl(void *dl, s32 color);
#endif

#endif // BINGO_NET_H
