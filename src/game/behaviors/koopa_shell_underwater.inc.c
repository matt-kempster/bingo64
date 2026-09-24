#include "game/bingo_tracking_collectables.h"

// koopa_shell_underwater.inc.c

struct ObjectHitbox sKoopaShellUnderwaterHitbox = {
    /* interactType:      */ INTERACT_GRABBABLE,
    /* downOffset:        */ 0,
    /* damageOrCoinValue: */ 0,
    /* health:            */ 1,
    /* numLootCoins:      */ 0,
    /* radius:            */ 80,
    /* height:            */ 50,
    /* hurtboxRadius:     */ 0,
    /* hurtboxHeight:     */ 0,
};

void set_koopa_shell_underwater_hitbox(void) {
    obj_set_hitbox(o, &sKoopaShellUnderwaterHitbox);
}

void bhv_koopa_shell_underwater_loop(void) {
    switch (o->oHeldState) {
        case HELD_FREE:
            set_koopa_shell_underwater_hitbox();
            // Bingo: the shell never moves until grabbed, so its spot is a
            // stable key for the Koopa shells objective.
            if (o->oBingoId == 0) {
                o->oBingoId = get_unique_id(BINGO_UPDATE_KOOPA_SHELL, o->oPosX, o->oPosY, o->oPosZ);
                if (o->oBingoId == (u32) -1) {
                    o->oBingoId = 0;
                }
            }
            break;
        case HELD_HELD:
            cur_obj_unrender_set_action_and_anim(-1, 0);
            break;
        case HELD_THROWN:
        case HELD_DROPPED:
            SWAP_PARTICLE_CALL(obj_mark_for_deletion(o), spawn_mist_particles());
            break;
    }

    if (o->oInteractStatus & INT_STATUS_STOP_RIDING) {
        SWAP_PARTICLE_CALL(obj_mark_for_deletion(o), spawn_mist_particles());
    }

    o->oInteractStatus = 0;
}
