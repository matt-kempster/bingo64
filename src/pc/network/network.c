// Online bingo client: TCP line protocol to server/relay.py, protocol v2.
// POSIX sockets on Linux/macOS, winsock2 on Windows. Connects without
// blocking so the file-select lobby stays responsive.

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
static s32 sLockout = 0;
static s32 sAutoReady = 0;
static s32 sLocalReady = 0;
static char sErrorMsg[64] = "";

// GO happens when gGlobalTimer reaches this (valid in COUNTDOWN/RACING).
static u32 sGoFrame = 0;


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

static void handle_line(char *line) {
    char cmd = line[0];
    // (log lines below are flushed so redirected logs survive a kill)
    if (cmd == 'W') {
        s32 lockout = 0, public_ = 0;
        s32 id = 0;
        if (sscanf(line + 1, "%d %d %d", &id, &lockout, &public_) == 3) {
            // A v1 relay sends "W <id> <seed> <lockout>": the huge seed
            // lands in the lockout field. Fail loudly instead of running
            // a half-broken lobby against an outdated server.
            if (lockout < 0 || lockout > 1 || public_ < 0 || public_ > 1) {
                net_fail("server runs an old relay version");
                return;
            }
            sLocalId = id;
            sLockout = lockout;
            sState = NET_STATE_LOBBY;
            printf("net: joined as #%d (%s room)\n", id, lockout ? "lockout" : "co-op");
            fflush(stdout);
            // If we created the room, our local bingo options become the
            // room's options (re-pushed if changed while in the lobby).
            network_push_local_options();
            if (sAutoReady) {
                network_set_ready(1);
            }
        }
    } else if (cmd == 'S') {
        u32 seed = 0;
        s32 delta = 0, target = 1, unlock = 0;
        unsigned long long mask = 0;
        if (sscanf(line + 1, "%u %d %d %d %llx", &seed, &delta, &target, &unlock, &mask) == 5) {
            s32 i;
            sSharedSeed = seed;
            sSeedValid = 1;
            // The room creator's bingo options apply to the whole room;
            // the board seeds identically for everyone.
            gbBingoTarget = target;
            gBingoFullGameUnlocked = (u8) (unlock != 0);
            for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT && i < 64; i++) {
                gBingoObjectivesDisabled[i] = (u8) ((mask >> i) & 1);
            }
            sGoFrame = (delta > 0) ? gGlobalTimer + (u32) delta : gGlobalTimer;
            sState = (delta > 0) ? NET_STATE_COUNTDOWN : NET_STATE_RACING;
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
        s32 id, color = 0, ready = 0;
        char name[NET_NAME_LEN] = "";
        if (sscanf(line + 1, "%d %15s %d %d", &id, name, &color, &ready) >= 3) {
            struct NetPlayer *p = player_for_id(id, 1);
            if (p != NULL) {
                strncpy(p->name, name, NET_NAME_LEN - 1);
                p->color = (u8) (color % NET_COLOR_COUNT);
                p->ready = (u8) (ready != 0);
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
    } else if (cmd == 'B') {
        s32 id;
        if (sscanf(line + 1, "%d", &id) == 1 && id >= 0 && id < MAX_ID) {
            struct NetPlayer *p = player_for_id(id, 0);
            if (p != NULL) {
                printf("net: %s left\n", p->name);
                fflush(stdout);
                p->active = 0;
            }
            if (sIdToSlot[id] >= 0) {
                gNetGhosts[(int) sIdToSlot[id]].active = 0;
                sIdToSlot[id] = -1;
            }
        }
    } else if (cmd == 'E') {
        char msg[48];
        snprintf(msg, sizeof(msg), "server refused: %s", line + 2);
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
    sLockout = 0;
    sLocalReady = 0;
    sClaimHead = sClaimTail = 0;
    sInLen = 0;
    sErrorMsg[0] = '\0';
}

s32 network_connect(const char *server, const char *room, const char *name,
                    s32 color, s32 flagsLockout, s32 flagsPublic,
                    u32 seedProposal) {
    char host[NET_SERVER_LEN];
    const char *colon;
    const char *port = "64064";
    char portbuf[16];
    struct addrinfo hints, *res = NULL, *ai;
    s32 flags = (flagsLockout ? 1 : 0) | (flagsPublic ? 2 : 0);

    network_disconnect();
    reset_session_state();

    if (server == NULL || server[0] == '\0') {
        return 0;
    }

#ifdef _WIN32
    {
        static s32 sWsaReady = 0;
        if (!sWsaReady) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                net_fail("WSAStartup failed");
                return 0;
            }
            sWsaReady = 1;
        }
    }
#endif

    colon = strrchr(server, ':');
    if (colon != NULL) {
        size_t hlen = colon - server;
        if (hlen >= sizeof(host)) {
            hlen = sizeof(host) - 1;
        }
        memcpy(host, server, hlen);
        host[hlen] = '\0';
        strncpy(portbuf, colon + 1, sizeof(portbuf) - 1);
        portbuf[sizeof(portbuf) - 1] = '\0';
        port = portbuf;
    } else {
        strncpy(host, server, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || res == NULL) {
        net_fail("cannot resolve server");
        return 0;
    }
    ai = res;
    sSocket = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sSocket == NET_BAD_SOCK) {
        freeaddrinfo(res);
        net_fail("cannot create socket");
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
        net_fail("cannot connect");
        return 0;
    }
    freeaddrinfo(res);

    snprintf(sJoinLine, sizeof(sJoinLine), "J %d %s %s %d %d %u\n",
             NET_PROTOCOL_VERSION,
             (room != NULL && room[0]) ? room : "bingo",
             (name != NULL && name[0]) ? name : "mario",
             color % NET_COLOR_COUNT, flags, seedProposal);
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
                    (s32) gCLIOpts.NetColor, 0, 0, 0);
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

    if (sState == NET_STATE_CONNECTING && sJoinLine[0] != '\0') {
        s32 done = poll_connect_done();
        if (done < 0) {
            net_fail("cannot connect");
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
           || sState == NET_STATE_RACING;
}

s32 network_local_id(void) {
    return sLocalId;
}

s32 network_lockout(void) {
    return sLockout;
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

void network_send_options(s32 target, s32 unlock, u64 mask) {
    char line[64];
    if (!network_active()) {
        return;
    }
    snprintf(line, sizeof(line), "O %d %d %llx\n",
             target, unlock, (unsigned long long) mask);
    net_send_line(line);
}

void network_push_local_options(void) {
    s32 i;
    u64 mask = 0;
    // Only the room creator's options count; the server drops the rest.
    if (!network_active() || sLocalId != 1) {
        return;
    }
    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT && i < 64; i++) {
        if (gBingoObjectivesDisabled[i]) {
            mask |= (u64) 1 << i;
        }
    }
    network_send_options(gbBingoTarget, gBingoFullGameUnlocked, mask);
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

#endif // NET_SOCKETS_AVAILABLE
