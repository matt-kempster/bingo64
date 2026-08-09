// Online bingo client: TCP line protocol to server/relay.py.
// POSIX sockets on Linux/macOS, winsock2 on Windows.

#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "macros.h"

#include "../cliopts.h"
#include "game/game_init.h"
#include "game/area.h"
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

static s32 sActive = 0;
static net_sock_t sSocket = NET_BAD_SOCK;
static s32 sLocalId = 0;
static u32 sSharedSeed = 0;
static s32 sSeedValid = 0;
static s32 sLockout = 0;

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

static void net_send_line(const char *line) {
    if (sSocket == NET_BAD_SOCK) {
        return;
    }
    if (send(sSocket, line, (int) strlen(line), 0) < 0 && !net_would_block()) {
        printf("net: connection lost (%s)\n", net_strerror());
        net_close(sSocket);
        sSocket = NET_BAD_SOCK;
        sActive = 0;
    }
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
            memset(&gNetGhosts[i], 0, sizeof(gNetGhosts[i]));
            gNetGhosts[i].active = 1;
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
        s32 lockout = 0;
        u32 seed = 0;
        s32 id = 0;
        if (sscanf(line + 1, "%d %u %d", &id, &seed, &lockout) == 3) {
            sLocalId = id;
            sSharedSeed = seed;
            sSeedValid = 1;
            sLockout = lockout;
            printf("net: joined as #%d, shared seed %u (%s mode)\n",
                   id, seed, lockout ? "lockout" : "co-op");
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
        s32 id;
        s32 team = 0;
        char name[NET_NAME_LEN] = "";
        if (sscanf(line + 1, "%d %15s %d", &id, name, &team) >= 2) {
            s32 slot = slot_for_id(id);
            if (slot >= 0) {
                strncpy(gNetGhosts[slot].name, name, NET_NAME_LEN - 1);
                gNetGhosts[slot].team = (u8) team;
                printf("net: %s is here (#%d, team %d)\n", name, id, team);
                fflush(stdout);
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
            if (sIdToSlot[id] >= 0) {
                printf("net: %s left\n", gNetGhosts[(int) sIdToSlot[id]].name);
                gNetGhosts[(int) sIdToSlot[id]].active = 0;
                sIdToSlot[id] = -1;
            }
        }
    } else if (cmd == 'E') {
        printf("net: server refused: %s\n", line + 2);
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
            }
            if (sInLen >= sizeof(sInBuf) - 1) {
                sInLen = 0;  // oversized line; drop it
            }
        } else if (n == 0) {
            printf("net: server closed the connection\n");
            net_close(sSocket);
            sSocket = NET_BAD_SOCK;
            sActive = 0;
            return;
        } else {
            if (!net_would_block()) {
                printf("net: recv error (%s)\n", net_strerror());
                net_close(sSocket);
                sSocket = NET_BAD_SOCK;
                sActive = 0;
            }
            return;
        }
    }
}

void network_init_from_cli(void) {
    char host[128];
    const char *colon;
    const char *port = "64064";
    char portbuf[16];
    struct addrinfo hints, *res = NULL, *ai;
    char line[192];
    s32 i;

    if (gCLIOpts.NetServer[0] == '\0') {
        return;
    }

#ifdef _WIN32
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            printf("net: WSAStartup failed\n");
            return;
        }
    }
#endif

    for (i = 0; i < MAX_ID; i++) {
        sIdToSlot[i] = -1;
    }

    colon = strrchr(gCLIOpts.NetServer, ':');
    if (colon != NULL) {
        size_t hlen = colon - gCLIOpts.NetServer;
        if (hlen >= sizeof(host)) {
            hlen = sizeof(host) - 1;
        }
        memcpy(host, gCLIOpts.NetServer, hlen);
        host[hlen] = '\0';
        strncpy(portbuf, colon + 1, sizeof(portbuf) - 1);
        portbuf[sizeof(portbuf) - 1] = '\0';
        port = portbuf;
    } else {
        strncpy(host, gCLIOpts.NetServer, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        printf("net: cannot resolve %s\n", gCLIOpts.NetServer);
        return;
    }
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        sSocket = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sSocket == NET_BAD_SOCK) {
            continue;
        }
        if (connect(sSocket, ai->ai_addr, (int) ai->ai_addrlen) == 0) {
            break;
        }
        net_close(sSocket);
        sSocket = NET_BAD_SOCK;
    }
    freeaddrinfo(res);
    if (sSocket == NET_BAD_SOCK) {
        printf("net: cannot connect to %s\n", gCLIOpts.NetServer);
        return;
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

    snprintf(line, sizeof(line), "J %s %s %u\n",
             gCLIOpts.NetRoom[0] ? gCLIOpts.NetRoom : "bingo",
             gCLIOpts.NetName[0] ? gCLIOpts.NetName : "mario",
             gCLIOpts.NetTeam);
    sActive = 1;
    net_send_line(line);
    printf("net: connecting to %s, room '%s'\n", gCLIOpts.NetServer,
           gCLIOpts.NetRoom[0] ? gCLIOpts.NetRoom : "bingo");
    fflush(stdout);
}

void network_shutdown(void) {
    if (sSocket != NET_BAD_SOCK) {
        net_close(sSocket);
        sSocket = NET_BAD_SOCK;
    }
    sActive = 0;
}

void network_update(void) {
    static u32 sLastSendTimer = 0;
    char line[160];

    if (!sActive) {
        return;
    }
    pump_inbound();
    if (!sActive) {
        return;
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

s32 network_active(void) {
    return sActive;
}

s32 network_has_seed(u32 *seed) {
    if (sSeedValid && seed != NULL) {
        *seed = sSharedSeed;
    }
    return sSeedValid;
}

void network_notify_local_claim(s32 cell) {
    char line[32];
    if (!sActive) {
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
