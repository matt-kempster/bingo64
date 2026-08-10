#ifndef PC_NETWORK_H
#define PC_NETWORK_H

#include <PR/ultratypes.h>

// Online bingo client (plans/online-bingo.md Part D).
// Talks to server/relay.py over TCP with a line-based text protocol,
// protocol version 2: join a room, ready up in the file-select lobby,
// receive the shared seed + room options in the S (start) message, then
// exchange ghost states and cell claims during the race.
// Everything here is PC-only; the N64 build never sees this header.

#define NET_PROTOCOL_VERSION 2

#define NET_MAX_GHOSTS  15
#define NET_MAX_PLAYERS 16
#define NET_NAME_LEN    16
#define NET_SERVER_LEN  128
#define NET_ROOM_LEN    32
#define NET_COLOR_COUNT 8

enum NetState {
    NET_STATE_OFF,        // not connected, no session
    NET_STATE_CONNECTING, // socket connecting / waiting for the welcome
    NET_STATE_LOBBY,      // in a room, waiting for everyone to ready up
    NET_STATE_COUNTDOWN,  // start message received, counting down to GO
    NET_STATE_RACING,     // GO happened; the race is on
    NET_STATE_ERROR       // connect failed or server refused; see
                          // network_error_message()
};

// A member of our room (including ourselves), for the lobby roster.
struct NetPlayer {
    u8 active;
    u8 ready;
    u8 color;   // palette index 0..NET_COLOR_COUNT-1
    s32 id;     // server-assigned id
    char name[NET_NAME_LEN];
};

struct NetGhost {
    u8 active;
    u8 color;   // palette index of the peer driving this ghost
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
extern struct NetPlayer gNetPlayers[NET_MAX_PLAYERS];

// The player palette: lobby roster text, ghost hat tint, etc.
extern const u8 gNetColorRGB[NET_COLOR_COUNT][3];

// Reads --net-server/--net-room/--net-name/--net-color from gCLIOpts,
// connects, and auto-readies (CLI sessions skip the lobby wait).
// Quietly does nothing when no server was requested.
void network_init_from_cli(void);
void network_shutdown(void);

// Start connecting to "host[:port]" and join a room. Non-blocking: poll
// network_state() for progress. flagsLockout/flagsPublic only matter when
// this join creates the room. Returns 0 if the connection could not even
// be started (bad host).
s32 network_connect(const char *server, const char *room, const char *name,
                    s32 color, s32 flagsLockout, s32 flagsPublic,
                    u32 seedProposal);
// Leave the room / abort the connection. Also clears NET_STATE_ERROR.
void network_disconnect(void);

// Pump inbound messages and send our ghost state. Call once per game frame.
void network_update(void);

enum NetState network_state(void);
const char *network_error_message(void);
s32 network_active(void);  // connected to a room (LOBBY or later)
s32 network_local_id(void);
s32 network_lockout(void);

// Ready-up. network_set_ready sends the toggle; the roster updates when
// the server echoes it.
void network_set_ready(s32 ready);
s32 network_local_ready(void);

// Frames (30/s) until GO. Positive during the countdown, 0 once racing.
s32 network_countdown_frames(void);

// Send the room's bingo options (server only accepts them from the room
// creator). mask bit i set = objective type i disabled. The options that
// arrive with the start message are applied to the bingo globals directly.
void network_send_options(s32 target, s32 unlock, u64 mask);
// Push the current bingo globals as room options (no-op unless we are
// the room creator). Called after the welcome and whenever the options
// screen may have changed them.
void network_push_local_options(void);

// The room's shared seed, valid once the start message arrived.
s32 network_has_seed(u32 *seed);

// Local player completed a board cell; tell the server.
void network_notify_local_claim(s32 cell);

// Dequeue one server-confirmed claim into *cell/*claimerId.
// Returns 0 when the queue is empty.
s32 network_poll_claim(s32 *cell, s32 *claimerId);

#endif // PC_NETWORK_H
