// Online bingo client: TCP line protocol to server/relay.py, protocol v3.
// POSIX sockets on Linux/macOS, winsock2 on Windows. Connects without
// blocking so the file-select lobby stays responsive. A dropped
// connection mid-session auto-reconnects with the server's token; the
// server restores our id and replays the room state.

#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "macros.h"

#include "../cliopts.h"
#include "game/game_init.h"
#include "game/area.h"
#include "game/bingo.h"
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
static s32 sPendingRoomMode = -1;  // room mode from W, applied once H tells
                                   // us whether we are the host

// The seed proposal from the seed-entry UI (0 = random); implemented in
// src/menu/file_select.c, linked into every PC build.
extern u32 bingo_seed_proposal(void);

// The join line, sent once the nonblocking connect completes.
static char sJoinLine[224];

// Inbound line assembly.
static char sInBuf[4096];
static u32 sInLen = 0;

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

static void net_send_line(const char *line) {
    if (sSocket == NET_BAD_SOCK) {
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
                    // The server dropped our seat; start over as a new member.
                    reset_room_state();
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
            sHostId = id;
            if (sHostId == sLocalId) {
                // Our local settings become (or stay) the room's.
                network_push_local_options();
            } else if (sPendingRoomMode >= 0) {
                gbBingoMode = (enum BingoGameMode) sPendingRoomMode;
            }
            sPendingRoomMode = -1;
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
            struct NetPlayer *p = player_for_id(id, 1);
            if (p != NULL) {
                strncpy(p->name, name, NET_NAME_LEN - 1);
                p->color = (u8) (color % NET_COLOR_COUNT);
                p->ready = (u8) (ready != 0);
                p->connected = (u8) (connected != 0);
                if (id != sLocalId) {
                    printf("net: %s is here (#%d, color %d)\n", name, id, color);
                    fflush(stdout);
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
                    p->active = 0;
                } else {
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
    } else if (cmd == 'E') {
        char msg[48];
        snprintf(msg, sizeof(msg), "server refused: %s", line + 2);
        sToken = 0;  // a refusal is final: no auto-reconnect loop
        net_fail(msg);
    }
}

static void pump_inbound(void) {
    if (sSocket == NET_BAD_SOCK) {
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
}

// Resolve the saved server, start the nonblocking connect and arm the
// join line (with the reconnect token if we have one). Returns 0 with
// *err set on immediate failure; no state change happens here.
static s32 net_dial(const char **err) {
    char host[NET_SERVER_LEN];
    const char *colon;
    const char *port = "64064";
    char portbuf[16];
    struct addrinfo hints, *res = NULL, *ai;

    if (sSocket != NET_BAD_SOCK) {
        net_close(sSocket);
        sSocket = NET_BAD_SOCK;
    }
    sInLen = 0;

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

    colon = strrchr(sSavedServer, ':');
    if (colon != NULL) {
        size_t hlen = colon - sSavedServer;
        if (hlen >= sizeof(host)) {
            hlen = sizeof(host) - 1;
        }
        memcpy(host, sSavedServer, hlen);
        host[hlen] = '\0';
        strncpy(portbuf, colon + 1, sizeof(portbuf) - 1);
        portbuf[sizeof(portbuf) - 1] = '\0';
        port = portbuf;
    } else {
        strncpy(host, sSavedServer, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
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
    {
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
static s32 poll_connect_done(void) {
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
