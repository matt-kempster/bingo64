// Online bingo, game side: ghost Mario puppets and shared cell claims.
// The PC network client (src/pc/network) feeds this; on N64 it compiles to
// nothing and the game is unchanged.

#include "bingo_net.h"

#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>

#include "area.h"
#include "behavior_data.h"
#include "bingo.h"
#include "bingo_ui.h"
#include "engine/math_util.h"
#include "game_init.h"
#include "level_update.h"
#include "memory.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "sm64.h"
#include "pc/network/network.h"

#define o gCurrentObject

// One Mario-animation DMA list per ghost so puppets don't fight the real
// Mario (or each other) over the shared animation buffer.
#define GHOST_ANIM_BUF_SIZE 0x4000
static struct DmaHandlerList sGhostAnimLists[NET_MAX_GHOSTS];
static u8 sGhostAnimBufs[NET_MAX_GHOSTS][GHOST_ANIM_BUF_SIZE];
static u8 sGhostAnimInit[NET_MAX_GHOSTS];

static struct Object *sGhostObjs[NET_MAX_GHOSTS];

// Set while applying a claim that came from the server, so the completion
// path in bingo.c does not echo it back.
u8 gBingoNetApplyingRemoteClaim = 0;

void bhv_net_ghost_update(void) {
    s32 slot = o->oBhvParams2ndByte;
    struct NetGhost *g;
    struct DmaHandlerList *list;
    struct Animation *targetAnim;

    if (slot < 0 || slot >= NET_MAX_GHOSTS) {
        obj_mark_for_deletion(o);
        return;
    }
    g = &gNetGhosts[slot];
    if (!g->active || g->level != gCurrLevelNum || g->area != gCurrAreaIndex) {
        sGhostObjs[slot] = NULL;
        obj_mark_for_deletion(o);
        return;
    }

    // Ease toward the latest network position; snap when far off (warp).
    if (ABS(o->oPosX - g->pos[0]) > 500.0f
        || ABS(o->oPosY - g->pos[1]) > 500.0f
        || ABS(o->oPosZ - g->pos[2]) > 500.0f) {
        o->oPosX = g->pos[0];
        o->oPosY = g->pos[1];
        o->oPosZ = g->pos[2];
    } else {
        o->oPosX += (g->pos[0] - o->oPosX) * 0.5f;
        o->oPosY += (g->pos[1] - o->oPosY) * 0.5f;
        o->oPosZ += (g->pos[2] - o->oPosZ) * 0.5f;
    }
    o->oFaceAngleYaw = g->yaw;
    o->oMoveAngleYaw = g->yaw;

    // Drive the Mario animation from the network state.
    list = &sGhostAnimLists[slot];
    if (!sGhostAnimInit[slot]) {
        // Share Mario's boot-allocated DMA table instead of calling
        // setup_dma_table_list: that would main_pool_alloc a copy mid-level,
        // which main_pool_pop_state frees on the next level transition,
        // leaving list->dmaTable dangling (crashed on castle entry).
        list->dmaTable = gMarioAnimsBuf.dmaTable;
        list->currentAddr = NULL;
        list->bufTarget = sGhostAnimBufs[slot];
        sGhostAnimInit[slot] = 1;
    }
    if (g->animID >= 0) {
        targetAnim = list->bufTarget;
        if (load_patchable_table(list, g->animID)) {
            targetAnim->values = (void *) VIRTUAL_TO_PHYSICAL((u8 *) targetAnim + (uintptr_t) targetAnim->values);
            targetAnim->index = (void *) VIRTUAL_TO_PHYSICAL((u8 *) targetAnim + (uintptr_t) targetAnim->index);
        }
        // Also compare curAnim: a recycled object can inherit a stale animID
        // that happens to match, with curAnim still NULL from geo_obj_init.
        if (o->header.gfx.animInfo.animID != g->animID
            || o->header.gfx.animInfo.curAnim != targetAnim) {
            o->header.gfx.animInfo.animID = g->animID;
            o->header.gfx.animInfo.curAnim = targetAnim;
            o->header.gfx.animInfo.animAccel = 0;
            o->header.gfx.animInfo.animYTrans = 0xBD;  // Mario's default
        }
        o->header.gfx.animInfo.animFrame = g->animFrame;
        o->header.gfx.node.flags |= GRAPH_RENDER_HAS_ANIMATION;
    }

    // Ghosts never interact: no hitbox was ever set, but make it explicit.
    o->oIntangibleTimer = -1;
}

static void spawn_missing_ghosts(void) {
    s32 i;
    if (gMarioObject == NULL) {
        return;
    }
    for (i = 0; i < NET_MAX_GHOSTS; i++) {
        struct NetGhost *g = &gNetGhosts[i];
        if (g->active && g->level == gCurrLevelNum && g->area == gCurrAreaIndex
            && g->lastUpdateFrame != 0 && sGhostObjs[i] == NULL) {
            struct Object *obj = spawn_object(gMarioObject, MODEL_MARIO, bhvNetGhost);
            if (obj != NULL) {
                obj->oBhvParams2ndByte = i;
                obj->oPosX = g->pos[0];
                obj->oPosY = g->pos[1];
                obj->oPosZ = g->pos[2];
                sGhostObjs[i] = obj;
            }
        } else if (sGhostObjs[i] != NULL
                   && (sGhostObjs[i]->activeFlags == ACTIVE_FLAG_DEACTIVATED
                       || sGhostObjs[i]->behavior != segmented_to_virtual(bhvNetGhost))) {
            sGhostObjs[i] = NULL;
        }
    }
}

// How server-confirmed claims land on the local board depends on the mode:
//   BLACKOUT  co-op: any member's claim completes the shared board.
//   LOCKOUT   (and CALLS) exclusive: the claim records the owner; completion state is
//             only set so the board shows the square as taken (tinted with
//             the owner's color). Counting is owner-based.
//   line modes  race: a peer's claim is recorded for display only; each
//             player completes their own board.
// Complete lines (rows, cols, both diagonals) a peer holds on the shared
// claim map; the BINGOS visibility tier announces these milestones and
// the roster shows them per player.
s32 bingo_net_bingo_count(s32 claimer) {
    u32 bit = (u32) 1 << claimer;
    s32 n = 0, i, j;
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5 && (gBingoCellClaimers[5 * i + j] & bit); j++) {}
        n += j == 5;
        for (j = 0; j < 5 && (gBingoCellClaimers[5 * j + i] & bit); j++) {}
        n += j == 5;
    }
    for (j = 0; j < 5 && (gBingoCellClaimers[6 * j] & bit); j++) {}
    n += j == 5;
    for (j = 0; j < 5 && (gBingoCellClaimers[4 * j + 4] & bit); j++) {}
    n += j == 5;
    return n;
}

// A mid-race disconnect must not re-attribute the board (the claim bits
// in gBingoCellClaimers survive, but sLocalId and gNetPlayers are wiped
// with the session, so everything used to render as "yours"): the local
// id and every peer's hat color are snapshotted each frame while
// connected, and the board reads the frozen copy once the connection
// dies. Cleared with the rest of the race state on room reset.
static s32 sFrozenLocalId = 0;
static u8 sFrozenColors[32];

static void freeze_ownership_snapshot(void) {
    s32 i;
    sFrozenLocalId = network_local_id();
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        const struct NetPlayer *p = &gNetPlayers[i];
        if (p->active && p->id >= 0 && p->id < 32) {
            sFrozenColors[p->id] = p->color;
        }
    }
}

s32 bingo_net_dropped(void) {
    return !network_active() && sFrozenLocalId != 0;
}

s32 bingo_net_display_id(void) {
    return network_active() ? network_local_id() : sFrozenLocalId;
}

s32 bingo_net_display_color(s32 id) {
    if (network_active()) {
        return network_color_of_id(id);
    }
    return (id >= 0 && id < 32) ? sFrozenColors[id] : 0;
}

static void apply_remote_claims(void) {
    s32 cell, claimer;
    while (network_poll_claim(&cell, &claimer)) {
        if (cell < 0 || cell >= 25) {
            continue;
        }
        if (claimer >= 0 && claimer < 32) {
            gBingoCellClaimers[cell] |= (u32) 1 << claimer;
        }
        if (claimer != network_local_id()) {
            // Toast the peer's progress. Since protocol v7 the relay
            // only sends us a peer's claim under the OPEN tier — the
            // other tiers arrive as aggregate M lines and toast in the
            // network client — so a peer claim here always shows rich.
            s32 i;
            for (i = 0; i < NET_MAX_PLAYERS; i++) {
                const struct NetPlayer *p = &gNetPlayers[i];
                if (!p->active || p->id != claimer) {
                    continue;
                }
                if (gNetClaimVis == NET_CLAIMVIS_OPEN) {
                    // Their name in their hat color, the square as its
                    // board icon plus its caption.
                    bingo_notice_rich(
                        p->name, gNetColorRGB[p->color % NET_COLOR_COUNT],
                        "completed", gBingoObjectives[cell].icon,
                        gBingoObjectives[cell].title);
                }
                break;
            }
        }
        if (gbBingoMode == BINGO_MODE_BLACKOUT || bingo_mode_exclusive()
            || claimer == network_local_id()) {
            gBingoNetApplyingRemoteClaim = 1;
            set_objective_state(&gBingoObjectives[cell], BINGO_STATE_COMPLETE);
            gBingoNetApplyingRemoteClaim = 0;
        }
    }
}

// After a reconnect the server replayed every claim it knows; send it the
// completions it missed while we were gone (it drops duplicates).
static void resend_missed_claims(void) {
    s32 i;
    u32 myBit = (u32) 1 << network_local_id();
    for (i = 0; i < 25; i++) {
        if (gBingoObjectives[i].state == BINGO_STATE_COMPLETE
            && !(gBingoCellClaimers[i] & myBit)) {
            network_notify_local_claim(i);
        }
    }
}

// Latched once we told the server we finished; cleared when the race
// state resets (bingo_net_on_room_reset) or the connection ends.
static s32 sFinishAnnounced = 0;

// The room went back to the lobby (host pressed BACK TO LOBBY, or a
// rejoin voided the race we were in): clear the local race so the next
// one sets up from scratch. Called from the network client's handler;
// the warp to the file select is driven separately by the lobby-return
// flag (see bingo_net_take_lobby_return / file_select).
void bingo_net_on_room_reset(void) {
    gBingoInitialized = 0;
    gbGlobalBingoTimer = 0;
    gbBingosCompleted = 0;
    gbBingoShowCongratsCounter = 0;
    sFinishAnnounced = 0;
    sFrozenLocalId = 0;
}

// EXIT GAME on the pause options menu: back out to the file select
// instead of killing the process (SamuRoy). Reuses the host-kick warp
// machinery below; network state (if any) is untouched, so online you
// stay in the room, same as when the host ends the race.
static s32 sLocalMenuReturn = 0;

void bingo_net_request_menu_return(void) {
    bingo_net_on_room_reset();
    sLocalMenuReturn = 1;
}

// Level-update poll: 1 exactly once after a room reset or a local EXIT
// GAME, meaning "leave the level and return to the file select lobby".
s32 bingo_net_take_lobby_return(void) {
    s32 local = sLocalMenuReturn;
    sLocalMenuReturn = 0;
    return local || network_take_lobby_return_flag();
}

// Service the connection outside play_mode_normal (pause and area/level
// transitions): keeps our G heartbeats and inbound pumping alive so the
// other players' rosters don't flip us to "?" during every load.
void bingo_net_keepalive(void) {
    network_update();
}

// Match history: the relay logs every racer's board verbatim (Y lines), so
// the history outlives the generator that dealt it. A cell is
// type.class.a.b.c with the dump_cell fields (test/host/test_bingo.c):
// stars course.star.limit (timed: seconds, click game: clicks), per-course
// course.toGet, multi-course total.each, Bowser level, else toGet. Trailing
// zeros are dropped.
static s32 sBoardSent = 0;
static u32 sBoardSentSeed = 0;

static s32 board_cell_code(char *buf, s32 size, s32 i) {
    struct BingoObjective *obj = &gBingoObjectives[i];
    s32 f[5];
    s32 n, len, k;
    f[0] = obj->type;
    f[1] = obj->class;
    f[2] = f[3] = f[4] = 0;
    switch (obj->type) {
        case BINGO_OBJECTIVE_STAR_TIMED:
            f[2] = obj->data.starTimerObjective.course;
            f[3] = obj->data.starTimerObjective.starIndex;
            f[4] = obj->data.starTimerObjective.maxTime / 30;
            break;
        case BINGO_OBJECTIVE_STAR_CLICK_GAME:
            f[2] = obj->data.starClicksObjective.course;
            f[3] = obj->data.starClicksObjective.starIndex;
            f[4] = obj->data.starClicksObjective.maxClicks;
            break;
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
            f[2] = obj->data.abcStarObjective.course;
            f[3] = obj->data.abcStarObjective.starIndex;
            break;
        case BINGO_OBJECTIVE_STAR:
        case BINGO_OBJECTIVE_STAR_TTC_RANDOM:
        case BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK:
        case BINGO_OBJECTIVE_STAR_GREEN_DEMON:
        case BINGO_OBJECTIVE_STAR_DAREDEVIL:
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_COINLESS:
            f[2] = obj->data.starObjective.course;
            f[3] = obj->data.starObjective.starIndex;
            break;
        case BINGO_OBJECTIVE_RANDOM_STARS:
        case BINGO_OBJECTIVE_COIN:
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
        case BINGO_OBJECTIVE_RANDOM_RED_COINS:
        case BINGO_OBJECTIVE_SPLATOON:
            f[2] = obj->data.courseCollectableData.course;
            f[3] = obj->data.courseCollectableData.toGet;
            break;
        case BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS:
        case BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS:
        case BINGO_OBJECTIVE_COINS_MULTIPLE_LEVELS:
            f[2] = obj->data.multiCourseCollectableData.toGetTotal;
            f[3] = obj->data.multiCourseCollectableData.toGetEachCourse;
            break;
        case BINGO_OBJECTIVE_BOWSER:
            f[2] = obj->data.levelData.level;
            break;
        default:
            f[2] = obj->data.collectableData.toGet;
            break;
    }
    n = 5;
    while (n > 2 && f[n - 1] == 0) {
        n--;
    }
    len = 0;
    for (k = 0; k < n && len < size; k++) {
        len += snprintf(buf + len, size - len, k ? ".%d" : "%d", f[k]);
    }
    return len < size ? len : size - 1;
}

static void send_board_history(void) {
    char line[480];
    s32 i, len = 0;
    u32 seed;
    if (network_state() != NET_STATE_RACING || !network_has_seed(&seed)
        || seed != gBingoInitialSeed) {
        return;  // not racing yet, or the board isn't this race's
    }
    if (sBoardSent && sBoardSentSeed == seed) {
        return;
    }
    sBoardSent = 1;
    sBoardSentSeed = seed;
    for (i = 0; i < 25 && len < (s32) sizeof(line) - 24; i++) {
        if (i) {
            line[len++] = ' ';
        }
        len += board_cell_code(line + len, sizeof(line) - len, i);
    }
    line[len] = '\0';
    network_send_board_line(line);
    if (gbBingoMode == BINGO_MODE_CALLS) {
        len = snprintf(line, sizeof(line), "q");
        for (i = 0; i < 25 && len < (s32) sizeof(line) - 4; i++) {
            len += snprintf(line + len, sizeof(line) - len, i ? ".%d" : " %d",
                            gBingoCallQueue[i]);
        }
        network_send_board_line(line);
    }
}

// Called once per gameplay frame from play_mode_normal.
void bingo_net_update(void) {
    if (!network_active()) {
        sFinishAnnounced = 0;
        sBoardSent = 0;
        return;
    }
    freeze_ownership_snapshot();
    spawn_missing_ghosts();
    if (gBingoInitialized) {
        send_board_history();
        apply_remote_claims();
        if (network_take_resync_flag()) {
            resend_missed_claims();
        }
        if (network_state() == NET_STATE_RACING
            || network_state() == NET_STATE_RECONNECTING) {
            // The room's shared clock, so every racer times from GO even
            // if they picked their file a little later. Once we have an
            // official result, freeze on the server's authoritative time.
            s32 i;
            const struct NetResult *mine = NULL;
            for (i = 0; i < network_result_count(); i++) {
                if (network_result(i)->id == network_local_id()) {
                    mine = network_result(i);
                }
            }
            if (mine != NULL) {
                gbGlobalBingoTimer = mine->frames;
            } else if (!bingo_race_over()) {
                gbGlobalBingoTimer = network_race_frames();
            }
            // Announce meeting the win condition (line modes; the server
            // itself decides lockout and calls from the claims).
            if (!bingo_mode_exclusive() && !sFinishAnnounced
                && bingo_race_won()) {
                sFinishAnnounced = 1;
                network_notify_local_finish();
            }
        }
    }
}

// Called by set_objective_state when a cell completes locally.
void bingo_net_on_local_complete(struct BingoObjective *objective) {
    if (gBingoNetApplyingRemoteClaim) {
        return;
    }
    if (!network_active()) {
        // A dropped race plays on offline: keep our own attribution
        // current so the frozen board (chips, lockout count) stays
        // truthful. Never set online: there the server echo owns the bit.
        if (sFrozenLocalId > 0) {
            gBingoCellClaimers[objective - gBingoObjectives] |=
                (u32) 1 << sFrozenLocalId;
        }
        return;
    }
    network_notify_local_claim(objective - gBingoObjectives);
}

s32 bingo_net_racing(void) {
    return network_active() && network_has_seed(NULL);
}

s32 bingo_net_local_cell_count(void) {
    s32 i, count = 0;
    u32 myBit = (u32) 1 << bingo_net_display_id();
    for (i = 0; i < 25; i++) {
        if (gBingoCellClaimers[i] & myBit) {
            count++;
        }
    }
    return count;
}

s32 bingo_net_race_decided(void) {
    return network_active() && network_race_winner_id() != 0;
}

s32 bingo_net_local_won(void) {
    return network_active()
           && network_race_winner_id() == network_local_id();
}

// The progress the room's tier lets us show for a player: our own (and
// everything under OPEN, or offline/dropped boards) comes from the local
// claim map; a peer's under a hidden tier comes from the relay's
// aggregate M lines (which is all we were sent).
s32 bingo_net_shown_cell_count(s32 id) {
    s32 n;
    if (id == bingo_net_display_id() || gNetClaimVis == NET_CLAIMVIS_OPEN
        || !network_active()) {
        s32 i, count = 0;
        u32 bit = (id >= 0 && id < 32) ? ((u32) 1 << id) : 0;
        for (i = 0; i < 25; i++) {
            if (gBingoCellClaimers[i] & bit) {
                count++;
            }
        }
        return count;
    }
    n = network_peer_cell_count(id);
    return n >= 0 ? n : 0;
}

s32 bingo_net_shown_bingo_count(s32 id) {
    s32 n;
    if (id == bingo_net_display_id() || gNetClaimVis == NET_CLAIMVIS_OPEN
        || !network_active()) {
        return bingo_net_bingo_count(id);
    }
    n = network_peer_bingo_count(id);
    return n >= 0 ? n : 0;
}

s32 bingo_net_race_timed_out(void) {
    return network_active() && network_race_timed_out();
}

s32 bingo_net_race_tiebreak(void) {
    return network_active() && network_race_won_by_tiebreak();
}

s32 bingo_net_obj_is_ghost(struct Object *obj) {
    return obj != NULL && obj->behavior == segmented_to_virtual(bhvNetGhost);
}

s32 bingo_net_ghost_color(struct Object *obj) {
    s32 slot = obj->oBhvParams2ndByte;
    if (slot < 0 || slot >= NET_MAX_GHOSTS) {
        return 0;
    }
    return gNetGhosts[slot].color % NET_COLOR_COUNT;
}

// ---------------------------------------------------------------------------
// Ghost hat colors. Mario's red parts (cap, its M logo's shading, arms,
// shirt) are lit by the shared `mario_red_lights_group`, referenced by
// pointer from static display lists that every Mario on screen executes.
// To recolor one ghost without touching the local player, the display
// lists a ghost renders are cloned (lazily, memoized) with those two
// light pointers swapped for a palette-colored Lights1. The clones share
// all vertex/texture data with the originals.

#include <stdlib.h>

extern const Lights1 mario_red_lights_group;

static Lights1 sGhostLights[NET_COLOR_COUNT];
static s32 sGhostLightsInit = 0;

static void init_ghost_lights(void) {
    s32 i, c;
    for (i = 0; i < NET_COLOR_COUNT; i++) {
        Lights1 *l = &sGhostLights[i];
        *l = mario_red_lights_group;
        for (c = 0; c < 3; c++) {
            l->l[0].l.col[c] = gNetColorRGB[i][c];
            l->l[0].l.colc[c] = gNetColorRGB[i][c];
            l->a.l.col[c] = gNetColorRGB[i][c] / 2;
            l->a.l.colc[c] = gNetColorRGB[i][c] / 2;
        }
    }
    sGhostLightsInit = 1;
}

// Memoized per-original-list scan/clone results.
#define TINT_TABLE_LEN 128
struct TintEntry {
    const Gfx *orig;
    s32 needsTint;
    Gfx *clone[NET_COLOR_COUNT];  // NULL until built; unused when !needsTint
};
static struct TintEntry sTintTable[TINT_TABLE_LEN];
static s32 sTintTableLen = 0;

static struct TintEntry *tint_entry_for(const Gfx *dl) {
    s32 i;
    for (i = 0; i < sTintTableLen; i++) {
        if (sTintTable[i].orig == dl) {
            return &sTintTable[i];
        }
    }
    if (sTintTableLen >= TINT_TABLE_LEN) {
        return NULL;
    }
    memset(&sTintTable[sTintTableLen], 0, sizeof(sTintTable[0]));
    sTintTable[sTintTableLen].orig = dl;
    sTintTable[sTintTableLen].needsTint = -1;  // not scanned yet
    return &sTintTable[sTintTableLen++];
}

#define DL_OPCODE(g) ((u32) ((g)->words.w0 >> 24) & 0xFF)
#define DL_IS_BRANCH(g) ((((g)->words.w0 >> 16) & 1) != 0)

static s32 dl_needs_tint(const Gfx *dl);

// Walk one display list; return 1 if it (or a list it calls) binds the
// red lights. Display lists here are DAGs ending in G_ENDDL.
static s32 dl_scan(const Gfx *dl) {
    const Gfx *g;
    for (g = dl;; g++) {
        u32 op = DL_OPCODE(g);
        if (op == (u32) (u8) G_ENDDL) {
            return 0;
        }
        if (g->words.w1 == (uintptr_t) &mario_red_lights_group.l
            || g->words.w1 == (uintptr_t) &mario_red_lights_group.a) {
            return 1;
        }
        if (op == (u32) G_DL) {
            if (dl_needs_tint((const Gfx *) g->words.w1)) {
                return 1;
            }
            if (DL_IS_BRANCH(g)) {
                return 0;  // tail branch: nothing follows in this list
            }
        }
    }
}

static s32 dl_needs_tint(const Gfx *dl) {
    struct TintEntry *e = tint_entry_for(dl);
    if (e == NULL) {
        return 0;  // table full: leave the list untinted
    }
    if (e->needsTint < 0) {
        e->needsTint = 0;  // break recursion; real DLs have no cycles
        e->needsTint = dl_scan(dl);
    }
    return e->needsTint;
}

static Gfx *dl_tinted_clone(const Gfx *dl, s32 color);

static Gfx *dl_build_clone(const Gfx *dl, s32 color) {
    const Gfx *g;
    Gfx *clone;
    s32 len = 0, i;
    for (g = dl;; g++, len++) {
        u32 op = DL_OPCODE(g);
        if (op == (u32) (u8) G_ENDDL || (op == (u32) G_DL && DL_IS_BRANCH(g))) {
            len++;
            break;
        }
    }
    clone = malloc(len * sizeof(Gfx));
    if (clone == NULL) {
        return NULL;
    }
    memcpy(clone, dl, len * sizeof(Gfx));
    for (i = 0; i < len; i++) {
        Gfx *c = &clone[i];
        if (c->words.w1 == (uintptr_t) &mario_red_lights_group.l) {
            c->words.w1 = (uintptr_t) &sGhostLights[color].l;
        } else if (c->words.w1 == (uintptr_t) &mario_red_lights_group.a) {
            c->words.w1 = (uintptr_t) &sGhostLights[color].a;
        } else if (DL_OPCODE(c) == (u32) G_DL
                   && dl_needs_tint((const Gfx *) c->words.w1)) {
            Gfx *sub = dl_tinted_clone((const Gfx *) c->words.w1, color);
            if (sub != NULL) {
                c->words.w1 = (uintptr_t) sub;
            }
        }
    }
    return clone;
}

static Gfx *dl_tinted_clone(const Gfx *dl, s32 color) {
    struct TintEntry *e = tint_entry_for(dl);
    if (e == NULL) {
        return NULL;
    }
    if (e->clone[color] == NULL) {
        e->clone[color] = dl_build_clone(dl, color);
    }
    return e->clone[color];
}

void *bingo_net_tinted_dl(void *dl, s32 color) {
    Gfx *clone;
    if (color <= 0 || color >= NET_COLOR_COUNT || dl == NULL) {
        return dl;
    }
    if (!sGhostLightsInit) {
        init_ghost_lights();
    }
    if (!dl_needs_tint((const Gfx *) dl)) {
        return dl;
    }
    clone = dl_tinted_clone((const Gfx *) dl, color);
    return clone != NULL ? (void *) clone : dl;
}

#else // TARGET_N64

void bhv_net_ghost_update(void) {
}

void bingo_net_update(void) {
}

void bingo_net_on_local_complete(UNUSED struct BingoObjective *objective) {
}

s32 bingo_net_obj_is_ghost(UNUSED struct Object *obj) {
    return 0;
}

s32 bingo_net_racing(void) {
    return 0;
}

s32 bingo_net_local_cell_count(void) {
    return 0;
}

s32 bingo_net_race_decided(void) {
    return 0;
}

s32 bingo_net_local_won(void) {
    return 0;
}

void bingo_net_on_room_reset(void) {
}

void bingo_net_request_menu_return(void) {
}

s32 bingo_net_take_lobby_return(void) {
    return 0;
}

void bingo_net_keepalive(void) {
}

s32 bingo_net_dropped(void) {
    return 0;
}

s32 bingo_net_race_timed_out(void) {
    return 0;
}

s32 bingo_net_race_tiebreak(void) {
    return 0;
}

s32 bingo_net_shown_cell_count(UNUSED s32 id) {
    return 0;
}

s32 bingo_net_shown_bingo_count(UNUSED s32 id) {
    return 0;
}

#endif
