#ifndef PC_NETWORK_H
#define PC_NETWORK_H

#include <PR/ultratypes.h>

// Online bingo client (plans/online-bingo.md Part D).
// Talks to server/relay.py over TCP with a line-based text protocol,
// protocol version 3: join a room, ready up in the file-select lobby,
// receive the shared seed + room options in the S (start) message, then
// exchange ghost states and cell claims during the race. The welcome
// carries a reconnect token: if the connection drops mid-race the client
// auto-reconnects with it and the server restores the same player id and
// replays the room state (claims, finishes).
// Everything here is PC-only; the N64 build never sees this header.

#define NET_PROTOCOL_VERSION 4

#define NET_MAX_GHOSTS  15
#define NET_MAX_PLAYERS 16
#define NET_NAME_LEN    16
#define NET_SERVER_LEN  128
#define NET_ROOM_LEN    32
#define NET_COLOR_COUNT 8

enum NetState {
    NET_STATE_OFF,          // not connected, no session
    NET_STATE_CONNECTING,   // socket connecting / waiting for the welcome
    NET_STATE_LOBBY,        // in a room, waiting for everyone to ready up
    NET_STATE_COUNTDOWN,    // start message received, counting down to GO
    NET_STATE_RACING,       // GO happened; the race is on
    NET_STATE_RECONNECTING, // connection dropped mid-session; retrying
                            // with the reconnect token
    NET_STATE_ERROR         // connect failed or server refused; see
                            // network_error_message()
};

// A member of our room (including ourselves), for the lobby roster.
struct NetPlayer {
    u8 active;
    u8 ready;
    u8 color;     // palette index 0..NET_COLOR_COUNT-1
    u8 connected; // 0 = dropped mid-race, may come back
    s32 id;       // server-assigned id
    char name[NET_NAME_LEN];
};

// One finisher, in placement order (relay-authoritative time in 30fps
// frames since GO, so every client shows identical results).
struct NetResult {
    s32 id;
    s32 place;
    s32 frames;
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
// network_state() for progress. flagsPublic only matters when this join
// creates the room; game mode and seed proposal ride along via
// network_push_local_options. Returns 0 if the connection could not even
// be started (bad host).
s32 network_connect(const char *server, const char *room, const char *name,
                    s32 color, s32 flagsPublic);

// Are we the room's host (the server decides; the role can pass on)?
s32 network_is_host(void);

// Host only: start the race now (ready marks are advisory; the host may
// force-start). The server answers with the S start broadcast.
void network_start_race(void);
// Leave the room / abort the connection. Also clears NET_STATE_ERROR.
void network_disconnect(void);

// Pump inbound messages and send our ghost state. Call once per game frame.
void network_update(void);

enum NetState network_state(void);
const char *network_error_message(void);
s32 network_active(void);  // connected to a room (LOBBY or later)
s32 network_local_id(void);
// The palette index of the room member with this id (0 if unknown).
s32 network_color_of_id(s32 id);

// Ready-up. network_set_ready sends the toggle; the roster updates when
// the server echoes it.
void network_set_ready(s32 ready);
s32 network_local_ready(void);

// Frames (30/s) until GO. Positive during the countdown, 0 once racing.
s32 network_countdown_frames(void);

// Send the room's settings (server only accepts them from the host).
// mask bit i set = objective type i disabled; seed 0 = random at start.
// The options that arrive with the start message are applied to the
// bingo globals directly.
void network_send_options(s32 mode, s32 unlock, u64 mask, u32 seed);
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

// Local player met the mode's win condition (line modes); tell the server
// once. The server assigns the placement and authoritative time.
void network_notify_local_finish(void);

// Frames (30/s) since GO; the room's shared race clock. 0 before GO.
s32 network_race_frames(void);

// Finish results, in placement order, as broadcast by the server.
s32 network_result_count(void);
const struct NetResult *network_result(s32 i);
// Place of the local player (0 = still racing).
s32 network_local_place(void);
// Lockout: the server-decided winner id (0 = not decided).
s32 network_race_winner_id(void);

// After a reconnect resync this returns 1 exactly once: the caller
// (bingo_net_update) re-sends any local completions the server missed.
s32 network_take_resync_flag(void);

#endif // PC_NETWORK_H
