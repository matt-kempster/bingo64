// Online bingo, game side: ghost Mario puppets and shared cell claims.
// The PC network client (src/pc/network) feeds this; on N64 it compiles to
// nothing and the game is unchanged.

#include "bingo_net.h"

#ifndef TARGET_N64

#include <string.h>

#include "area.h"
#include "behavior_data.h"
#include "bingo.h"
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

static void apply_remote_claims(void) {
    s32 cell, claimer;
    while (network_poll_claim(&cell, &claimer)) {
        if (cell >= 0 && cell < 25) {
            gBingoNetApplyingRemoteClaim = 1;
            set_objective_state(&gBingoObjectives[cell], BINGO_STATE_COMPLETE);
            gBingoNetApplyingRemoteClaim = 0;
        }
    }
}

// Called once per gameplay frame from play_mode_normal.
void bingo_net_update(void) {
    if (!network_active()) {
        return;
    }
    spawn_missing_ghosts();
    if (gBingoInitialized) {
        apply_remote_claims();
    }
}

// Called by set_objective_state when a cell completes locally.
void bingo_net_on_local_complete(struct BingoObjective *objective) {
    if (!network_active() || gBingoNetApplyingRemoteClaim) {
        return;
    }
    network_notify_local_claim(objective - gBingoObjectives);
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

#endif
