// clam.inc.c
#include "game/bingo.h"
#include "game/bingo_tracking_collectables.h"

struct ObjectHitbox sClamShellHitbox = {
    /* interactType:      */ INTERACT_CLAM_OR_BUBBA,
    /* downOffset:        */ 0,
    /* damageOrCoinValue: */ 2,
    /* health:            */ 99,
    /* numLootCoins:      */ 0,
    /* radius:            */ 150,
    /* height:            */ 80,
    /* hurtboxRadius:     */ 150,
    /* hurtboxHeight:     */ 80,
};

void clam_act_0(void) {
#if CLAMS_ONLY_HURT_WHEN_MOVING
    o->oDamageOrCoinValue = 0;
#endif
    if (cur_obj_init_anim_check_frame(0, 25)) {
        cur_obj_play_sound_2(SOUND_GENERAL_CLAM_SHELL_CLOSE);
        spawn_mist_from_global();
        cur_obj_become_tangible();
#if CLAMS_ONLY_HURT_WHEN_MOVING
        o->oDamageOrCoinValue = 2;
#endif
        o->oClamUnkF4 = 10;
        o->oTimer = 0;
    } else if (o->oTimer > 150 && o->oDistanceToMario < 500.0f) {
        cur_obj_play_sound_2(SOUND_GENERAL_CLAM_SHELL_OPEN);
        o->oAction = 1;
    } else if (o->oClamUnkF4 != 0) {
        o->oClamUnkF4--;
        cur_obj_shake_y(3.0f);
    }
}

void clam_act_1(void) {
    s16 val06;
#if CLAMS_ONLY_HURT_WHEN_MOVING
    o->oDamageOrCoinValue = 0;
#endif

    if (o->oTimer > 150) {
        o->oAction = 0;
    } else if (obj_is_rendering_enabled() && cur_obj_init_anim_check_frame(1, 8)) {
        for (val06 = -0x2000; val06 < 0x2000; val06 += 0x555) {
            s16 val04 = (s16)(100.0f * sins(val06));
            s16 val02 = (s16)(100.0f * coss(val06));

            spawn_object_relative(0, val04, 30, val02, o, MODEL_BUBBLE, bhvBubbleMaybe);
        }
    } else if (cur_obj_check_anim_frame(30)) {
        cur_obj_become_intangible();
    }
}

void bhv_clam_loop(void) {
    o->header.gfx.scale[1] = 1.5f;

    // bhvClamShell has no SET_HOME, and clam_act_0 shakes oPosY, so latch the
    // UID from the clam's very first position and keep it.
    if (o->oBingoId == 0) {
        o->oBingoId = get_unique_id(BINGO_UPDATE_BITTEN_BY_CLAM, o->oPosX, o->oPosY, o->oPosZ);
    }

    switch (o->oAction) {
        case 0:
            clam_act_0();
            break;
        case 1:
            clam_act_1();
            break;
    }

    // Same shape as the amp's "hurt by" credit: take_damage_and_knock_back
    // sets INT_STATUS_ATTACKED_MARIO when the shell actually bites Mario.
    if (o->oInteractStatus & INT_STATUS_ATTACKED_MARIO) {
        if (is_new_kill(BINGO_UPDATE_BITTEN_BY_CLAM, o->oBingoId)) {
            bingo_update(BINGO_UPDATE_BITTEN_BY_CLAM);
        }
    }

    obj_check_attacks(&sClamShellHitbox, o->oAction);
}
