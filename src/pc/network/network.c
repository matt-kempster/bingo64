// Online bingo client: line protocol to server/relay.py, protocol v4.
// POSIX sockets on Linux/macOS, winsock2 on Windows. Connects without
// blocking so the file-select lobby stays responsive. A dropped
// connection mid-session auto-reconnects with the server's token; the
// server restores our id and replays the room state.
//
// Two transports, chosen by the server address ("udp:" prefix = UDP,
// otherwise TCP). TCP is the newline-framed stream it has always been.
// UDP exists for tunnels that only carry UDP (playit.gg free tier):
// each datagram is a 10-byte header + one line, control messages ride a
// small reliable in-order channel and ghost state rides an unreliable-
// sequenced channel where a lost packet is simply superseded by the
// next one. The reference implementation of this transport is
// RefClient in server/test_relay.py; keep the two in sync.

#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "macros.h"

#include "../cliopts.h"
#include "game/game_init.h"
#include "game/area.h"
#include "game/bingo.h"
#include "game/bingo_net.h"
#include "game/bingo_ui.h"
#include "game/level_update.h"
#include "game/object_list_processor.h"
#include "engine/math_util.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET net_sock_t;
#define NET_BAD_SOCK INVALID_SOCKET
#define net_close closesocket
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int net_sock_t;
#define NET_BAD_SOCK (-1)
#define net_close close
#endif
#define NET_SOCKETS_AVAILABLE 1

// send/recv failed only because the nonblocking socket has no data/space?
static s32 net_would_block(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

// connect() on a nonblocking socket reported "still in progress"?
static s32 net_connect_in_progress(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EINPROGRESS;
#endif
}

static const char *net_strerror(void) {
#ifdef _WIN32
    static char buf[32];
    snprintf(buf, sizeof(buf), "winsock error %d", WSAGetLastError());
    return buf;
#else
    return strerror(errno);
#endif
}

struct NetGhost gNetGhosts[NET_MAX_GHOSTS];
struct NetPlayer gNetPlayers[NET_MAX_PLAYERS];

// Index 0 must stay Mario red: it maps to the unmodified model.
const u8 gNetColorRGB[NET_COLOR_COUNT][3] = {
    { 255, 70, 70 },   { 70, 220, 70 },   { 100, 140, 255 }, { 255, 220, 60 },
    { 200, 100, 255 }, { 255, 150, 210 }, { 90, 230, 255 },  { 240, 240, 240 },
};

static enum NetState sState = NET_STATE_OFF;
static net_sock_t sSocket = NET_BAD_SOCK;
static s32 sLocalId = 0;
static u32 sSharedSeed = 0;
static s32 sSeedValid = 0;
static s32 sAutoReady = 0;
static s32 sLocalReady = 0;
static char sErrorMsg[64] = "";

// GO happens when gGlobalTimer reaches this (valid in COUNTDOWN/RACING).
static u32 sGoFrame = 0;

// Reconnect state: the server's token from the welcome, the join
// parameters to redial with, and the retry timer.
static u32 sToken = 0;
static char sSavedServer[NET_SERVER_LEN];
static char sSavedRoom[NET_ROOM_LEN];
static char sSavedName[NET_NAME_LEN];
static s32 sSavedColor = 0;
static s32 sSavedFlags = 0;
static s32 sRetryFrames = 0;
static s32 sWasReconnect = 0;  // this connection resumed a session
static s32 sResyncFlag = 0;

// Race results as broadcast by the server (placement order).
static struct NetResult sResults[NET_MAX_PLAYERS];
static s32 sResultCount = 0;
static s32 sWinnerId = 0;      // lockout: the decided winner
static s32 sFinishSent = 0;
static s32 sHostId = 0;        // who may edit settings and start the race
static s32 sGoFlag = 0;        // set once when we enter RACING; file select
                               // consumes it to auto-launch the game at GO
static s32 sLobbyReturnFlag = 0;  // set once when the room resets to the
                                  // lobby; the game warps to file select
static s32 sPendingRoomMode = -1;  // room mode from W, applied once H tells
                                   // us whether we are the host

// The seed proposal from the seed-entry UI (0 = random); implemented in
// src/menu/file_select.c, linked into every PC build.
extern u32 bingo_seed_proposal(void);

// The join line, sent once the nonblocking connect completes.
static char sJoinLine[224];

// Inbound line assembly (TCP framing).
static char sInBuf[4096];
static u32 sInLen = 0;

// --- UDP transport (server address prefixed "udp:") -------------------
// Header (big-endian): u8 magic, u8 type, u32 conn_id, u32 seq.
// conn_id is a random nonzero id we pick per dial; it identifies the
// session in both directions so NAT/proxy address changes don't matter.
#define UDP_MAGIC       0xB6
#define UDP_T_GHOST     0  // seq = ghost counter, stale packets dropped
#define UDP_T_RELIABLE  1  // seq = 1-based message sequence
#define UDP_T_ACK       2  // seq = highest reliable seq received in order
#define UDP_T_KEEPALIVE 3
#define UDP_HEADER_LEN  10
#define UDP_MSG_MAX     224
#define UDP_OUT_RING    32   // unacked reliable messages (a reconnect
                             // resync can burst ~25 claim lines)
#define UDP_RESEND_FRAMES    8    // ~266 ms between retransmit bursts
#define UDP_KEEPALIVE_FRAMES 90   // 3 s; keeps NAT mappings open and
                                  // makes the server ack (liveness)
#define UDP_TIMEOUT_FRAMES   450  // 15 s of server silence = dead link

static s32 sUdpMode = 0;
static u32 sConnId = 0;
static char sOutMsg[UDP_OUT_RING][UDP_MSG_MAX];
static u16  sOutMsgLen[UDP_OUT_RING];
static u32  sOutMsgSeq[UDP_OUT_RING];
static s32  sOutHead = 0, sOutCount = 0;
static u32  sOutNext = 1;       // next reliable seq to assign
static u32  sInExpected = 0;    // highest reliable seq processed in order
static u32  sGhostIn = 0, sGhostOut = 0;
static u32  sLastInboundFrame = 0;
static u32  sLastResendFrame = 0, sLastKeepaliveFrame = 0;

static void udp_store_u32(u8 *p, u32 v) {
    p[0] = (u8) (v >> 24);
    p[1] = (u8) (v >> 16);
    p[2] = (u8) (v >> 8);
    p[3] = (u8) v;
}

static u32 udp_load_u32(const u8 *p) {
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
}

// Server-confirmed claims waiting for the game loop to apply them.
#define CLAIM_QUEUE_LEN 32
static s32 sClaimCells[CLAIM_QUEUE_LEN];
static s32 sClaimIds[CLAIM_QUEUE_LEN];
static s32 sClaimHead = 0, sClaimTail = 0;

// id -> ghost slot mapping (server ids are small integers).
#define MAX_ID 64
static s8 sIdToSlot[MAX_ID];

#ifdef NET_SOCKETS_AVAILABLE

static void net_fail(const char *msg) {
    if (sSocket != NET_BAD_SOCK) {
        net_close(sSocket);
        sSocket = NET_BAD_SOCK;
    }
    // A live session with a token redials instead of giving up; the
    // server holds our seat and replays the room state when we return.
    if (sToken != 0
        && (sState == NET_STATE_LOBBY || sState == NET_STATE_COUNTDOWN
            || sState == NET_STATE_RACING || sState == NET_STATE_RECONNECTING)) {
        if (sState != NET_STATE_RECONNECTING) {
            printf("net: connection lost (%s), reconnecting\n", msg);
            fflush(stdout);
        }
        sState = NET_STATE_RECONNECTING;
        sJoinLine[0] = '\0';
        sRetryFrames = 30;
        return;
    }
    snprintf(sErrorMsg, sizeof(sErrorMsg), "%s", msg);
    sState = NET_STATE_ERROR;
    printf("net: %s\n", sErrorMsg);
    fflush(stdout);
}

// Send one datagram. Send errors are deliberately ignored: UDP sends
// fail transiently (ICMP unreachable after a server restart, full
// buffers); the silence timeout in udp_tick is the real detector.
static void udp_send_dgram(u8 type, u32 seq, const char *payload, u32 len) {
    u8 buf[UDP_HEADER_LEN + UDP_MSG_MAX];
    if (sSocket == NET_BAD_SOCK || len > UDP_MSG_MAX) {
        return;
    }
    buf[0] = UDP_MAGIC;
    buf[1] = type;
    udp_store_u32(buf + 2, sConnId);
    udp_store_u32(buf + 6, seq);
    memcpy(buf + UDP_HEADER_LEN, payload, len);
    send(sSocket, (const char *) buf, (int) (UDP_HEADER_LEN + len), 0);
}

static void udp_send_line(const char *line) {
    u32 len = (u32) strlen(line);
    s32 slot;
    while (len > 0 && line[len - 1] == '\n') {
        len--;  // datagram boundaries frame messages; no newline needed
    }
    if (line[0] == 'G' && line[1] == ' ') {
        udp_send_dgram(UDP_T_GHOST, ++sGhostOut, line, len);
        return;
    }
    if (sOutCount >= UDP_OUT_RING) {
        // The server stopped acking long enough to back us up; redial
        // (the token + resync flow replays anything that mattered).
        net_fail("network backlog");
        return;
    }
    if (len >= UDP_MSG_MAX) {
        len = UDP_MSG_MAX - 1;
    }
    slot = (sOutHead + sOutCount) % UDP_OUT_RING;
    memcpy(sOutMsg[slot], line, len);
    sOutMsgLen[slot] = (u16) len;
    sOutMsgSeq[slot] = sOutNext++;
    sOutCount++;
    udp_send_dgram(UDP_T_RELIABLE, sOutMsgSeq[slot], sOutMsg[slot], len);
}

static void net_send_line(const char *line) {
    if (sSocket == NET_BAD_SOCK) {
        return;
    }
    if (sUdpMode) {
        udp_send_line(line);
        return;
    }
    if (send(sSocket, line, (int) strlen(line), 0) < 0 && !net_would_block()) {
        net_fail("connection lost");
    }
}

static struct NetPlayer *player_for_id(s32 id, s32 createIfMissing) {
    s32 i, freeSlot = -1;
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        if (gNetPlayers[i].active && gNetPlayers[i].id == id) {
            return &gNetPlayers[i];
        }
        if (!gNetPlayers[i].active && freeSlot < 0) {
            freeSlot = i;
        }
    }
    if (createIfMissing && freeSlot >= 0) {
        memset(&gNetPlayers[freeSlot], 0, sizeof(gNetPlayers[freeSlot]));
        gNetPlayers[freeSlot].active = 1;
        gNetPlayers[freeSlot].id = id;
        return &gNetPlayers[freeSlot];
    }
    return NULL;
}

static s32 slot_for_id(s32 id) {
    s32 i;
    if (id < 0 || id >= MAX_ID) {
        return -1;
    }
    if (sIdToSlot[id] >= 0) {
        return sIdToSlot[id];
    }
    for (i = 0; i < NET_MAX_GHOSTS; i++) {
        if (!gNetGhosts[i].active) {
            struct NetPlayer *p = player_for_id(id, 0);
            memset(&gNetGhosts[i], 0, sizeof(gNetGhosts[i]));
            gNetGhosts[i].active = 1;
            if (p != NULL) {
                strncpy(gNetGhosts[i].name, p->name, NET_NAME_LEN - 1);
                gNetGhosts[i].color = p->color;
            }
            sIdToSlot[id] = i;
            return i;
        }
    }
    return -1;
}

// Forget everything scoped to the current race — seed, results, claims,
// ghosts, the finish latch — keeping the roster and our seat. Runs when
// the room returns to the lobby (K) and when a rejoin voided our old
// race; either way the game side resets its board and, if we are in a
// level, warps back to the file select (the lobby-return flag).
static void reset_race_state(void) {
    s32 i;
    for (i = 0; i < MAX_ID; i++) {
        sIdToSlot[i] = -1;
    }
    memset(gNetGhosts, 0, sizeof(gNetGhosts));
    sClaimHead = sClaimTail = 0;
    sResultCount = 0;
    sWinnerId = 0;
    sFinishSent = 0;
    sGoFlag = 0;
    sSeedValid = 0;
    sSharedSeed = 0;
    sGoFrame = 0;
    sLocalReady = 0;
    bingo_net_on_room_reset();
    sLobbyReturnFlag = 1;
}

// Forget every room member and ghost (a reconnect that came back with a
// fresh id: the server no longer knew us, so the old session is void).
static void reset_room_state(void) {
    s32 i;
    for (i = 0; i < MAX_ID; i++) {
        sIdToSlot[i] = -1;
    }
    memset(gNetGhosts, 0, sizeof(gNetGhosts));
    memset(gNetPlayers, 0, sizeof(gNetPlayers));
    sClaimHead = sClaimTail = 0;
    sResultCount = 0;
    sWinnerId = 0;
}

// "<name> <did something>" as an in-game toast (bingo_notice uppercases).
static void notice_about(s32 id, const char *what) {
    struct NetPlayer *p = player_for_id(id, 0);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %s", p != NULL ? p->name : "someone", what);
    bingo_notice(buf);
}

static const char *place_suffix(s32 place) {
    if (place == 1) return "ST";
    if (place == 2) return "ND";
    if (place == 3) return "RD";
    return "TH";
}

static void handle_line(char *line) {
    char cmd = line[0];
    // (log lines below are flushed so redirected logs survive a kill)
    if (cmd == 'W') {
        s32 public_ = 0, mode = 0;
        s32 id = 0;
        u32 token = 0;
        if (sscanf(line + 1, "%d %d %d %u", &id, &public_, &mode, &token) == 4) {
            // A v1/v2 relay packs other fields here; fail loudly instead
            // of running a half-broken lobby against an outdated server.
            if (public_ < 0 || public_ > 1 || mode < 0 || mode >= BINGO_MODE_COUNT) {
                sToken = 0;
                net_fail("server runs an old relay version");
                return;
            }
            if (sWasReconnect && id == sLocalId) {
                printf("net: reconnected as #%d\n", id);
                sResyncFlag = 1;
            } else {
                if (sWasReconnect) {
                    // The server dropped our seat; start over as a new
                    // member. The race we were in is void too (if the
                    // room is mid-race after all, the S replay follows
                    // and re-enters us as a late joiner).
                    reset_room_state();
                    reset_race_state();
                    sResyncFlag = 1;
                }
                printf("net: joined as #%d\n", id);
            }
            fflush(stdout);
            sLocalId = id;
            sToken = token;
            // The room's current mode; applied in the H handler once we
            // know whether we are the host (whose own push wins).
            sPendingRoomMode = mode;
            // A started room's S replay follows immediately and advances
            // us back to COUNTDOWN/RACING.
            sState = NET_STATE_LOBBY;
            if (sAutoReady) {
                network_set_ready(1);
            }
        }
    } else if (cmd == 'H') {
        s32 id;
        if (sscanf(line + 1, "%d", &id) == 1) {
            s32 prevHost = sHostId;
            sHostId = id;
            if (sHostId == sLocalId) {
                // Our local settings become (or stay) the room's.
                network_push_local_options();
            } else if (sPendingRoomMode >= 0) {
                gbBingoMode = (enum BingoGameMode) sPendingRoomMode;
            }
            sPendingRoomMode = -1;
            // The role passed on (0 = the initial announcement).
            if (prevHost != 0 && id != prevHost) {
                if (id == sLocalId) {
                    bingo_notice("you are now the host");
                } else {
                    notice_about(id, "is now the host");
                }
            }
        }
    } else if (cmd == 'S') {
        u32 seed = 0;
        s32 delta = 0, mode = 0, unlock = 0;
        unsigned long long mask = 0;
        if (sscanf(line + 1, "%u %d %d %d %llx", &seed, &delta, &mode, &unlock, &mask) == 5) {
            s32 i;
            sSharedSeed = seed;
            sSeedValid = 1;
            // The room creator's bingo options apply to the whole room;
            // the board seeds identically for everyone.
            if (mode >= 0 && mode < BINGO_MODE_COUNT) {
                gbBingoMode = (enum BingoGameMode) mode;
            }
            gBingoFullGameUnlocked = (u8) (unlock != 0);
            for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT && i < 64; i++) {
                gBingoObjectivesDisabled[i] = (u8) ((mask >> i) & 1);
            }
            // delta is negative when the race already started (late join /
            // reconnect); unsigned wrap keeps the shared clock correct.
            sGoFrame = gGlobalTimer + (u32) delta;
            sState = (delta > 0) ? NET_STATE_COUNTDOWN : NET_STATE_RACING;
            if (delta <= 0) {
                sGoFlag = 1;
            }
            printf("net: race starts in %d frames, seed %u\n", delta, seed);
            fflush(stdout);
        }
    } else if (cmd == 'G') {
        s32 id, level, area, yaw, animID, animFrame;
        f32 x, y, z;
        if (sscanf(line + 1, "%d %d %d %f %f %f %d %d %d",
                   &id, &level, &area, &x, &y, &z, &yaw, &animID, &animFrame) == 9) {
            s32 slot = slot_for_id(id);
            if (slot >= 0) {
                struct NetGhost *g = &gNetGhosts[slot];
                if (g->lastUpdateFrame == 0) {
                    printf("net: first ghost state from #%d (level %d area %d)\n", id, level, area);
                    fflush(stdout);
                }
                g->prevPos[0] = g->pos[0];
                g->prevPos[1] = g->pos[1];
                g->prevPos[2] = g->pos[2];
                if (g->level != level || g->area != area || g->lastUpdateFrame == 0) {
                    // Teleported (area change / first packet): don't interpolate.
                    g->prevPos[0] = x;
                    g->prevPos[1] = y;
                    g->prevPos[2] = z;
                }
                g->pos[0] = x;
                g->pos[1] = y;
                g->pos[2] = z;
                g->level = level;
                g->area = area;
                g->yaw = (s16) yaw;
                g->animID = (s16) animID;
                g->animFrame = (s16) animFrame;
                g->lastUpdateFrame = gGlobalTimer;
            }
        }
    } else if (cmd == 'N') {
        s32 id, color = 0, ready = 0, connected = 1;
        char name[NET_NAME_LEN] = "";
        if (sscanf(line + 1, "%d %15s %d %d %d", &id, name, &color, &ready, &connected) >= 3) {
            struct NetPlayer *existing = player_for_id(id, 0);
            s32 wasConnected = (existing != NULL) ? existing->connected : -1;
            struct NetPlayer *p = player_for_id(id, 1);
            if (p != NULL) {
                strncpy(p->name, name, NET_NAME_LEN - 1);
                p->color = (u8) (color % NET_COLOR_COUNT);
                p->ready = (u8) (ready != 0);
                p->connected = (u8) (connected != 0);
                if (id != sLocalId) {
                    printf("net: %s is here (#%d, color %d)\n", name, id, color);
                    fflush(stdout);
                    if (wasConnected == 0 && connected != 0) {
                        notice_about(id, "reconnected");
                    } else if (wasConnected < 0
                               && (sState == NET_STATE_COUNTDOWN
                                   || sState == NET_STATE_RACING)) {
                        notice_about(id, "joined the race");
                    }
                }
            }
        }
    } else if (cmd == 'R') {
        s32 id, ready;
        if (sscanf(line + 1, "%d %d", &id, &ready) == 2) {
            struct NetPlayer *p = player_for_id(id, 0);
            if (p != NULL) {
                p->ready = (u8) (ready != 0);
            }
            if (id == sLocalId) {
                sLocalReady = ready != 0;
            }
        }
    } else if (cmd == 'C') {
        s32 cell, id;
        if (sscanf(line + 1, "%d %d", &cell, &id) == 2) {
            s32 next = (sClaimTail + 1) % CLAIM_QUEUE_LEN;
            if (next != sClaimHead) {
                sClaimCells[sClaimTail] = cell;
                sClaimIds[sClaimTail] = id;
                sClaimTail = next;
            }
        }
    } else if (cmd == 'B' || cmd == 'D') {
        s32 id;
        if (sscanf(line + 1, "%d", &id) == 1 && id >= 0 && id < MAX_ID) {
            struct NetPlayer *p = player_for_id(id, 0);
            if (p != NULL) {
                printf("net: %s %s\n", p->name,
                       cmd == 'B' ? "left" : "disconnected (may return)");
                fflush(stdout);
                if (cmd == 'B') {
                    notice_about(id, "left");
                    p->active = 0;
                } else {
                    notice_about(id, "disconnected");
                    p->connected = 0;
                    p->ready = 0;
                }
            }
            if (sIdToSlot[id] >= 0) {
                gNetGhosts[(int) sIdToSlot[id]].active = 0;
                sIdToSlot[id] = -1;
            }
        }
    } else if (cmd == 'O') {
        // The host changed the room options while we sat in the lobby.
        s32 mode;
        if (sscanf(line + 1, "%d", &mode) == 1
            && mode >= 0 && mode < BINGO_MODE_COUNT && sLocalId != sHostId) {
            gbBingoMode = (enum BingoGameMode) mode;
        }
    } else if (cmd == 'F') {
        s32 id, place, frames;
        if (sscanf(line + 1, "%d %d %d", &id, &place, &frames) == 3) {
            s32 i, known = 0;
            for (i = 0; i < sResultCount; i++) {
                known |= sResults[i].id == id;
            }
            if (!known && sResultCount < NET_MAX_PLAYERS) {
                sResults[sResultCount].id = id;
                sResults[sResultCount].place = place;
                sResults[sResultCount].frames = frames;
                sResultCount++;
                printf("net: #%d finished in place %d (%d frames)\n", id, place, frames);
                fflush(stdout);
                if (id != sLocalId) {
                    // (our own finish gets the big win overlay instead)
                    char what[32];
                    snprintf(what, sizeof(what), "finished %d%s",
                             place, place_suffix(place));
                    notice_about(id, what);
                }
            }
        }
    } else if (cmd == 'V') {
        s32 id, frames;
        if (sscanf(line + 1, "%d %d", &id, &frames) == 2) {
            sWinnerId = id;
            if (sResultCount == 0) {
                sResults[0].id = id;
                sResults[0].place = 1;
                sResults[0].frames = frames;
                sResultCount = 1;
            }
            printf("net: race decided, #%d wins (%d frames)\n", id, frames);
            fflush(stdout);
        }
    } else if (cmd == 'K') {
        // The host ended the race: the whole room is back in the lobby.
        // The server re-sends the roster (everyone unready) right after.
        printf("net: back to the lobby\n");
        fflush(stdout);
        // Fresh enough to still be showing on the lobby's status line
        // once the warp lands everyone back at the file select.
        bingo_notice("the host ended the race");
        reset_race_state();
        if (network_active()) {
            sState = NET_STATE_LOBBY;
        }
    } else if (cmd == 'E') {
        char msg[48];
        snprintf(msg, sizeof(msg), "server refused: %s", line + 2);
        sToken = 0;  // a refusal is final: no auto-reconnect loop
        net_fail(msg);
    }
}

static void pump_inbound_udp(void) {
    u8 buf[UDP_HEADER_LEN + 600 + 1];
    for (;;) {
        u8 type;
        u32 seq;
        char *payload;
        int n = (int) recv(sSocket, (char *) buf, (int) sizeof(buf) - 1, 0);
        if (n < 0) {
            if (!net_would_block()) {
                // Connected UDP sockets surface ICMP unreachable here
                // (ECONNREFUSED / WSAECONNRESET): the server is gone.
                net_fail("connection lost");
            }
            return;
        }
        if (n < UDP_HEADER_LEN || buf[0] != UDP_MAGIC
            || udp_load_u32(buf + 2) != sConnId) {
            continue;
        }
        sLastInboundFrame = gGlobalTimer;
        type = buf[1];
        seq = udp_load_u32(buf + 6);
        buf[n] = '\0';
        payload = (char *) buf + UDP_HEADER_LEN;
        if (type == UDP_T_ACK) {
            while (sOutCount > 0 && (s32) (seq - sOutMsgSeq[sOutHead]) >= 0) {
                sOutHead = (sOutHead + 1) % UDP_OUT_RING;
                sOutCount--;
            }
        } else if (type == UDP_T_KEEPALIVE) {
            udp_send_dgram(UDP_T_ACK, sInExpected, "", 0);
        } else if (type == UDP_T_GHOST) {
            if ((s32) (seq - sGhostIn) > 0) {
                sGhostIn = seq;
                handle_line(payload);
            }
        } else if (type == UDP_T_RELIABLE) {
            if (seq == sInExpected + 1) {
                sInExpected = seq;
                udp_send_dgram(UDP_T_ACK, sInExpected, "", 0);
                handle_line(payload);
            } else {
                // Duplicate or gap-jumper: drop it, re-state what we
                // have; the server resends the missing seq shortly.
                udp_send_dgram(UDP_T_ACK, sInExpected, "", 0);
            }
        }
        if (sSocket == NET_BAD_SOCK) {
            return;  // a handler failed the connection
        }
    }
}

// Retransmit unacked reliable messages, keep the NAT mapping warm, and
// notice a dead server. Called once per frame while the socket is up.
static void udp_tick(void) {
    s32 i, burst;
    if (sSocket == NET_BAD_SOCK) {
        return;
    }
    if (gGlobalTimer - sLastResendFrame >= UDP_RESEND_FRAMES) {
        sLastResendFrame = gGlobalTimer;
        burst = sOutCount < 8 ? sOutCount : 8;
        for (i = 0; i < burst; i++) {
            s32 slot = (sOutHead + i) % UDP_OUT_RING;
            udp_send_dgram(UDP_T_RELIABLE, sOutMsgSeq[slot], sOutMsg[slot],
                           sOutMsgLen[slot]);
        }
    }
    if (gGlobalTimer - sLastKeepaliveFrame >= UDP_KEEPALIVE_FRAMES) {
        sLastKeepaliveFrame = gGlobalTimer;
        udp_send_dgram(UDP_T_KEEPALIVE, 0, "", 0);
    }
    if (gGlobalTimer - sLastInboundFrame > UDP_TIMEOUT_FRAMES) {
        net_fail("server not responding");
    }
}

static void pump_inbound(void) {
    if (sSocket == NET_BAD_SOCK) {
        return;
    }
    if (sUdpMode) {
        pump_inbound_udp();
        return;
    }
    for (;;) {
        int n = (int) recv(sSocket, sInBuf + sInLen, (int) (sizeof(sInBuf) - sInLen - 1), 0);
        if (n > 0) {
            char *nl;
            sInLen += n;
            sInBuf[sInLen] = '\0';
            while ((nl = strchr(sInBuf, '\n')) != NULL) {
                *nl = '\0';
                handle_line(sInBuf);
                sInLen -= (nl + 1 - sInBuf);
                memmove(sInBuf, nl + 1, sInLen + 1);
                if (sSocket == NET_BAD_SOCK) {
                    return;  // a handler failed the connection
                }
            }
            if (sInLen >= sizeof(sInBuf) - 1) {
                sInLen = 0;  // oversized line; drop it
            }
        } else if (n == 0) {
            net_fail("server closed the connection");
            return;
        } else {
            if (!net_would_block()) {
                net_fail("connection lost");
            }
            return;
        }
    }
}

static void reset_session_state(void) {
    s32 i;
    for (i = 0; i < MAX_ID; i++) {
        sIdToSlot[i] = -1;
    }
    memset(gNetGhosts, 0, sizeof(gNetGhosts));
    memset(gNetPlayers, 0, sizeof(gNetPlayers));
    sLocalId = 0;
    sSeedValid = 0;
    sSharedSeed = 0;
    sLocalReady = 0;
    sClaimHead = sClaimTail = 0;
    sInLen = 0;
    sErrorMsg[0] = '\0';
    sToken = 0;
    sRetryFrames = 0;
    sWasReconnect = 0;
    sResyncFlag = 0;
    sResultCount = 0;
    sWinnerId = 0;
    sFinishSent = 0;
    sHostId = 0;
    sPendingRoomMode = -1;
    sGoFlag = 0;
    sLobbyReturnFlag = 0;
}

// Resolve the saved server, start the nonblocking connect and arm the
// join line (with the reconnect token if we have one). Returns 0 with
// *err set on immediate failure; no state change happens here.
static s32 net_dial(const char **err) {
    char host[NET_SERVER_LEN];
    const char *addr = sSavedServer;
    const char *colon;
    const char *port = "64064";
    char portbuf[16];
    struct addrinfo hints, *res = NULL, *ai;

    if (sSocket != NET_BAD_SOCK) {
        net_close(sSocket);
        sSocket = NET_BAD_SOCK;
    }
    sInLen = 0;

    sUdpMode = 0;
    if (strncmp(addr, "udp:", 4) == 0) {
        sUdpMode = 1;
        addr += 4;
    }
    // Fresh transport state per dial: a new session id (a reconnect is a
    // new UDP session; continuity comes from the token in the join line)
    // and zeroed sequence/ack/ring state.
    sConnId = (u32) time(NULL) ^ ((u32) (uintptr_t) &hints << 8)
              ^ (gGlobalTimer * 2654435761u);
    if (sConnId == 0) {
        sConnId = 1;
    }
    sOutHead = sOutCount = 0;
    sOutNext = 1;
    sInExpected = 0;
    sGhostIn = sGhostOut = 0;
    sLastInboundFrame = gGlobalTimer;
    sLastResendFrame = sLastKeepaliveFrame = gGlobalTimer;

#ifdef _WIN32
    {
        static s32 sWsaReady = 0;
        if (!sWsaReady) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                *err = "WSAStartup failed";
                return 0;
            }
            sWsaReady = 1;
        }
    }
#endif

    colon = strrchr(addr, ':');
    if (colon != NULL) {
        size_t hlen = colon - addr;
        if (hlen >= sizeof(host)) {
            hlen = sizeof(host) - 1;
        }
        memcpy(host, addr, hlen);
        host[hlen] = '\0';
        strncpy(portbuf, colon + 1, sizeof(portbuf) - 1);
        portbuf[sizeof(portbuf) - 1] = '\0';
        port = portbuf;
    } else {
        strncpy(host, addr, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = sUdpMode ? SOCK_DGRAM : SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || res == NULL) {
        *err = "cannot resolve server";
        return 0;
    }
    ai = res;
    sSocket = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sSocket == NET_BAD_SOCK) {
        freeaddrinfo(res);
        *err = "cannot create socket";
        return 0;
    }
#ifdef _WIN32
    {
        u_long nonblock = 1;
        ioctlsocket(sSocket, FIONBIO, &nonblock);
    }
#else
    fcntl(sSocket, F_SETFL, O_NONBLOCK);
#endif
    if (!sUdpMode) {
        int one = 1;
        setsockopt(sSocket, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof(one));
    }
    if (connect(sSocket, ai->ai_addr, (int) ai->ai_addrlen) != 0 && !net_connect_in_progress()) {
        freeaddrinfo(res);
        net_close(sSocket);
        sSocket = NET_BAD_SOCK;
        *err = "cannot connect";
        return 0;
    }
    freeaddrinfo(res);

    snprintf(sJoinLine, sizeof(sJoinLine), "J %d %s %s %d %d %u\n",
             NET_PROTOCOL_VERSION, sSavedRoom, sSavedName,
             sSavedColor, sSavedFlags, sToken);
    sWasReconnect = sToken != 0;
    return 1;
}

s32 network_connect(const char *server, const char *room, const char *name,
                    s32 color, s32 flagsPublic) {
    const char *err = "cannot connect";

    network_disconnect();
    reset_session_state();

    if (server == NULL || server[0] == '\0') {
        return 0;
    }
    snprintf(sSavedServer, sizeof(sSavedServer), "%s", server);
    snprintf(sSavedRoom, sizeof(sSavedRoom), "%s",
             (room != NULL && room[0]) ? room : "bingo");
    snprintf(sSavedName, sizeof(sSavedName), "%s",
             (name != NULL && name[0]) ? name : "mario");
    sSavedColor = color % NET_COLOR_COUNT;
    sSavedFlags = flagsPublic ? 1 : 0;

    if (!net_dial(&err)) {
        net_fail(err);
        return 0;
    }
    sState = NET_STATE_CONNECTING;
    printf("net: connecting to %s\n", server);
    fflush(stdout);
    return 1;
}

void network_disconnect(void) {
    if (sUdpMode && sSocket != NET_BAD_SOCK && network_active()) {
        // Courtesy quit so the room hears B/D now instead of after the
        // server's silence timeout. Fire-and-forget: we are closing, so
        // send a few copies and let the timeout cover the loss case.
        s32 i;
        u32 seq = sOutNext++;
        for (i = 0; i < 3; i++) {
            udp_send_dgram(UDP_T_RELIABLE, seq, "Q", 1);
        }
    }
    if (sSocket != NET_BAD_SOCK) {
        net_close(sSocket);
        sSocket = NET_BAD_SOCK;
    }
    sState = NET_STATE_OFF;
    sErrorMsg[0] = '\0';
    sAutoReady = 0;
    reset_session_state();
}

void network_init_from_cli(void) {
    if (gCLIOpts.NetServer[0] == '\0') {
        return;
    }
    network_connect(gCLIOpts.NetServer,
                    gCLIOpts.NetRoom[0] ? gCLIOpts.NetRoom : "bingo",
                    gCLIOpts.NetName[0] ? gCLIOpts.NetName : "mario",
                    (s32) gCLIOpts.NetColor, 0);
    // CLI sessions ready up automatically: whoever launches from the
    // command line doesn't want to hang around in the lobby. (Set after
    // network_connect: its internal disconnect clears the flag.)
    sAutoReady = 1;
}

void network_shutdown(void) {
    network_disconnect();
}

// Has the nonblocking connect finished? 1 = yes, 0 = still going, -1 = failed.
// UDP "connects" instantly (it only fixes the peer address).
static s32 poll_connect_done(void) {
    if (sUdpMode) {
        return 1;
    }
    {
    fd_set wfds, efds;
    struct timeval tv;
    int err = 0;
#ifdef _WIN32
    int errlen = sizeof(err);
#else
    socklen_t errlen = sizeof(err);
#endif
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    FD_SET(sSocket, &wfds);
    FD_SET(sSocket, &efds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select((int) sSocket + 1, NULL, &wfds, &efds, &tv) <= 0) {
        return 0;
    }
    if (getsockopt(sSocket, SOL_SOCKET, SO_ERROR, (char *) &err, &errlen) != 0 || err != 0) {
        return -1;
    }
    return FD_ISSET(sSocket, &wfds) ? 1 : 0;
    }
}

void network_update(void) {
    static u32 sLastSendTimer = 0;
    char line[160];

    if (sState == NET_STATE_OFF || sState == NET_STATE_ERROR) {
        return;
    }

    // Between reconnect attempts: no socket, just a retry countdown.
    if (sState == NET_STATE_RECONNECTING && sSocket == NET_BAD_SOCK) {
        if (--sRetryFrames <= 0) {
            const char *err;
            if (net_dial(&err)) {
                printf("net: redialing %s\n", sSavedServer);
                fflush(stdout);
            } else {
                sRetryFrames = 150;  // resolve failed; try again in 5s
            }
        }
        return;
    }

    if (sJoinLine[0] != '\0'
        && (sState == NET_STATE_CONNECTING || sState == NET_STATE_RECONNECTING)) {
        s32 done = poll_connect_done();
        if (done < 0) {
            if (sState == NET_STATE_RECONNECTING) {
                net_close(sSocket);
                sSocket = NET_BAD_SOCK;
                sJoinLine[0] = '\0';
                sRetryFrames = 90;
            } else {
                net_fail("cannot connect");
            }
            return;
        }
        if (done > 0) {
            net_send_line(sJoinLine);
            sJoinLine[0] = '\0';
        }
        return;
    }

    pump_inbound();
    if (sUdpMode) {
        udp_tick();
    }
    if (sState == NET_STATE_OFF || sState == NET_STATE_ERROR) {
        return;
    }

    if (sState == NET_STATE_COUNTDOWN && gGlobalTimer >= sGoFrame) {
        sState = NET_STATE_RACING;
        sGoFlag = 1;
    }

    // Ghost state at 15Hz (every other 30Hz game frame): plenty smooth with
    // client-side interpolation, half the traffic.
    if (gMarioObject != NULL && gGlobalTimer != sLastSendTimer && (gGlobalTimer & 1) == 0) {
        sLastSendTimer = gGlobalTimer;
        snprintf(line, sizeof(line), "G %d %d %.1f %.1f %.1f %d %d %d\n",
                 gCurrLevelNum, gCurrAreaIndex,
                 gMarioObject->header.gfx.pos[0],
                 gMarioObject->header.gfx.pos[1],
                 gMarioObject->header.gfx.pos[2],
                 (s32) gMarioObject->header.gfx.angle[1],
                 (s32) gMarioObject->header.gfx.animInfo.animID,
                 (s32) gMarioObject->header.gfx.animInfo.animFrame);
        net_send_line(line);
    }
}

enum NetState network_state(void) {
    return sState;
}

const char *network_error_message(void) {
    return sErrorMsg;
}

s32 network_active(void) {
    return sState == NET_STATE_LOBBY || sState == NET_STATE_COUNTDOWN
           || sState == NET_STATE_RACING || sState == NET_STATE_RECONNECTING;
}

s32 network_local_id(void) {
    return sLocalId;
}

s32 network_color_of_id(s32 id) {
    struct NetPlayer *p = player_for_id(id, 0);
    return p != NULL ? p->color : 0;
}

void network_set_ready(s32 ready) {
    char line[16];
    if (sState != NET_STATE_LOBBY) {
        return;
    }
    snprintf(line, sizeof(line), "R %d\n", ready ? 1 : 0);
    net_send_line(line);
}

s32 network_local_ready(void) {
    return sLocalReady;
}

s32 network_countdown_frames(void) {
    if (sState == NET_STATE_COUNTDOWN && sGoFrame > gGlobalTimer) {
        return (s32) (sGoFrame - gGlobalTimer);
    }
    return 0;
}

void network_send_options(s32 mode, s32 unlock, u64 mask, u32 seed) {
    char line[80];
    if (!network_active()) {
        return;
    }
    snprintf(line, sizeof(line), "O %d %d %llx %u\n",
             mode, unlock, (unsigned long long) mask, seed);
    net_send_line(line);
}

void network_push_local_options(void) {
    s32 i;
    u64 mask = 0;
    // Only the host's settings count; the server drops the rest.
    if (!network_is_host()) {
        return;
    }
    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT && i < 64; i++) {
        if (gBingoObjectivesDisabled[i]) {
            mask |= (u64) 1 << i;
        }
    }
    network_send_options((s32) gbBingoMode, gBingoFullGameUnlocked, mask,
                         bingo_seed_proposal());
}

s32 network_is_host(void) {
    return network_active() && sHostId != 0 && sLocalId == sHostId;
}

s32 network_host_id(void) {
    return sHostId;
}

s32 network_room_locked(void) {
    return sState == NET_STATE_COUNTDOWN || sState == NET_STATE_RACING
           || sState == NET_STATE_RECONNECTING;
}

s32 network_take_go_flag(void) {
    s32 flag = sGoFlag;
    sGoFlag = 0;
    return flag;
}

void network_start_race(void) {
    if (network_is_host() && sState == NET_STATE_LOBBY) {
        net_send_line("X\n");
    }
}

void network_request_lobby(void) {
    if (network_is_host() && (sState == NET_STATE_COUNTDOWN
                              || sState == NET_STATE_RACING)) {
        net_send_line("K\n");
    }
}

s32 network_take_lobby_return_flag(void) {
    s32 flag = sLobbyReturnFlag;
    sLobbyReturnFlag = 0;
    return flag;
}

s32 network_has_seed(u32 *seed) {
    if (sSeedValid && seed != NULL) {
        *seed = sSharedSeed;
    }
    return sSeedValid;
}

void network_notify_local_claim(s32 cell) {
    char line[32];
    if (!network_active()) {
        return;
    }
    snprintf(line, sizeof(line), "C %d\n", cell);
    net_send_line(line);
}

s32 network_poll_claim(s32 *cell, s32 *claimerId) {
    if (sClaimHead == sClaimTail) {
        return 0;
    }
    *cell = sClaimCells[sClaimHead];
    *claimerId = sClaimIds[sClaimHead];
    sClaimHead = (sClaimHead + 1) % CLAIM_QUEUE_LEN;
    return 1;
}

void network_notify_local_finish(void) {
    if (!network_active() || sFinishSent) {
        return;
    }
    sFinishSent = 1;
    net_send_line("F\n");
}

s32 network_race_frames(void) {
    if (sSeedValid && (sState == NET_STATE_RACING
                       || sState == NET_STATE_RECONNECTING)) {
        return (s32) (gGlobalTimer - sGoFrame);
    }
    return 0;
}

s32 network_result_count(void) {
    return sResultCount;
}

const struct NetResult *network_result(s32 i) {
    if (i < 0 || i >= sResultCount) {
        return NULL;
    }
    return &sResults[i];
}

s32 network_local_place(void) {
    s32 i;
    for (i = 0; i < sResultCount; i++) {
        if (sResults[i].id == sLocalId) {
            return sResults[i].place;
        }
    }
    return 0;
}

s32 network_race_winner_id(void) {
    return sWinnerId;
}

s32 network_take_resync_flag(void) {
    s32 f = sResyncFlag;
    sResyncFlag = 0;
    return f;
}

#endif // NET_SOCKETS_AVAILABLE
