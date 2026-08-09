// ddd_pole.inc.c
#include "game/bingo.h"

void bhv_ddd_pole_init(void) {
    s32 shouldDelete = 0;
    if (gBingoFullGameUnlocked) {
        shouldDelete = gCurrActNum == 1;
    } else {
        shouldDelete = !(save_file_get_flags() & (SAVE_FLAG_HAVE_KEY_2 | SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR));
    }
    if (shouldDelete) {
        obj_mark_for_deletion(o);
    } else {
        o->hitboxDownOffset = 100.0f;
        o->oDDDPoleMaxOffset = 100.0f * o->oBhvParams2ndByte;
        o->oBingoId = get_unique_id(BINGO_UPDATE_GRABBED_POLE, o->oPosX, o->oPosY, o->oPosZ);
    }
}

void bhv_ddd_pole_update(void) {
    if (o->oTimer > 20) {
        o->oDDDPoleOffset += o->oDDDPoleVel;

        if (clamp_f32(&o->oDDDPoleOffset, 0.0f, o->oDDDPoleMaxOffset)) {
            o->oDDDPoleVel = -o->oDDDPoleVel;
            o->oTimer = 0;
        }
    }

    obj_set_dist_from_home(o->oDDDPoleOffset);
}
