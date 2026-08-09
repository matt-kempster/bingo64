#ifndef PC_NETWORK_H
#define PC_NETWORK_H

#include <PR/ultratypes.h>

// Online bingo client (plans/online-bingo.md Part D).
// Talks to server/relay.py over TCP with a line-based text protocol.
// Everything here is PC-only; the N64 build never sees this header.

#define NET_MAX_GHOSTS 15
#define NET_NAME_LEN   16

struct NetGhost {
    u8 active;
    u8 team;    // 0 = no team; group-battle modes key off this
    char name[NET_NAME_LEN];
    s16 level;
    s16 area;
    // Latest state from the network, and the previous one for interpolation.
    f32 pos[3];
    f32 prevPos[3];
    s16 yaw;
    s16 animID;
    s16 animFrame;
    u32 lastUpdateFrame;  // gGlobalTimer when the last packet applied
};

extern struct NetGhost gNetGhosts[NET_MAX_GHOSTS];

// Reads --net-server/--net-room/--net-name from gCLIOpts and connects.
// Quietly does nothing when no server was requested.
void network_init_from_cli(void);
void network_shutdown(void);

// Pump inbound messages and send our ghost state. Call once per game frame.
void network_update(void);

s32 network_active(void);

// The room's shared seed, valid once the server's welcome arrived.
s32 network_has_seed(u32 *seed);

// Local player completed a board cell; tell the server.
void network_notify_local_claim(s32 cell);

// Dequeue one server-confirmed claim into *cell/*claimerId.
// Returns 0 when the queue is empty.
s32 network_poll_claim(s32 *cell, s32 *claimerId);

#endif // PC_NETWORK_H
