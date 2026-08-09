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
