// spawn_star.c.inc

#include "game/bingo.h"
#include "../bingo_tracking_star.h"

static struct ObjectHitbox sCollectStarHitbox = {
    /* interactType:      */ INTERACT_STAR_OR_KEY,
    /* downOffset:        */ 0,
    /* damageOrCoinValue: */ 0,
    /* health:            */ 0,
    /* numLootCoins:      */ 0,
    /* radius:            */ 80,
    /* height:            */ 50,
    /* hurtboxRadius:     */ 0,
    /* hurtboxHeight:     */ 0,
};

#include "game/bingo_rando_spawn.h"
#include "game/bingo.h"
#include "engine/rand.h"
void bhv_rando_star_init(void) {
    Vec3s pos;
    u16 seedCopy = gBingoInitialSeed;

    s8 sp1F;
    u8 sp1E;

    if (!gBingoRandomStarsActive) {
        mark_obj_for_deletion(o);
        return;
    }

    sp1F = (o->oBehParams >> 24) & 0xFF;
    sp1E = bingo_get_rando_star_status(gCurrCourseNum, sp1F);
    if (sp1E) {
        o->header.gfx.sharedChild = gLoadedGraphNodes[MODEL_TRANSPARENT_STAR];
    } else {
        o->header.gfx.sharedChild = gLoadedGraphNodes[MODEL_STAR_PURPLE];
    }
    init_genrand(seedCopy);
    for (sp1E = 0; sp1E < gCurrCourseNum + sp1F; sp1E++) {
        // Here's something kind of nitpicky: I don't
        // want the stars to appear in the same (x,y,z)
        // position in two different courses in case peopl
        // remember that e.g. two are right next to each other.
        // So we call RandomU16() the same number of times as the
        // current course.
        seedCopy ^= RandomU16();
    }
    o->oInteractionSubtype |= (INT_SUBTYPE_RANDO_STAR | INT_SUBTYPE_NO_EXIT);

    set_object_hitbox(o, &sCollectStarHitbox);
    get_safe_position(
        o,
        pos,
        400.f,
        700.f,
        &seedCopy,
        FLOOR_SAFE_HOVERING,
        (
            RAND_POSITION_FLAG_CAN_BE_UNDERWATER
            | RAND_POSITION_FLAG_THI_A3_ABOVE_MESH
            | RAND_POSITION_FLAG_SPAWN_BOTTOM_OF_SLIDE
            | RAND_POSITION_FLAG_BBH_HMC_LIMITED_ROOMS
        )
    );
    o->oPosX = pos[0];
    o->oPosY = pos[1];
    o->oPosZ = pos[2];
}

void bhv_collect_star_init(void) {
    s8 sp1F;
    u8 sp1E;

    sp1F = (o->oBehParams >> 24) & 0xFF;
    // sp1E = save_file_get_star_flags(gCurrSaveFileNum - 1, gCurrCourseNum - 1);
    sp1E = bingo_get_course_flags(gCurrCourseNum - 1);
    if (sp1E & (1 << sp1F)) {
        o->header.gfx.sharedChild = gLoadedGraphNodes[MODEL_TRANSPARENT_STAR];
    } else {
        o->header.gfx.sharedChild = gLoadedGraphNodes[MODEL_STAR];
    }

    set_object_hitbox(o, &sCollectStarHitbox);
}

void bhv_collect_star_loop(void) {
    o->oFaceAngleYaw += 0x800;

    if (o->oInteractStatus & INT_STATUS_INTERACTED) {
        mark_obj_for_deletion(o);
        o->oInteractStatus = 0;
    }
}

void bhv_star_spawn_init(void) {
    o->oMoveAngleYaw = atan2s(o->oHomeZ - o->oPosZ, o->oHomeX - o->oPosX);
    o->oStarSpawnDisFromHome = sqrtf(sqr(o->oHomeX - o->oPosX) + sqr(o->oHomeZ - o->oPosZ));
    o->oVelY = (o->oHomeY - o->oPosY) / 30.0f;
    o->oForwardVel = o->oStarSpawnDisFromHome / 30.0f;
    o->oStarSpawnUnkFC = o->oPosY;
    if (o->oBehParams2ndByte == 0 || gCurrCourseNum == COURSE_BBH)
        cutscene_object(CUTSCENE_STAR_SPAWN, o);
    else
        cutscene_object(CUTSCENE_RED_COIN_STAR_SPAWN, o);

    set_time_stop_flags(TIME_STOP_ENABLED | TIME_STOP_MARIO_AND_DOORS);
    o->activeFlags |= 0x20;
    obj_become_intangible();
}

void bhv_star_spawn_loop(void) {
    switch (o->oAction) {
        case 0:
            o->oFaceAngleYaw += 0x1000;
            if (o->oTimer > 20)
                o->oAction = 1;
            break;

        case 1:
            obj_move_xyz_using_fvel_and_yaw(o);
            o->oStarSpawnUnkFC += o->oVelY;
            o->oPosY = o->oStarSpawnUnkFC + sins((o->oTimer * 0x8000) / 30) * 400.0f;
            o->oFaceAngleYaw += 0x1000;
            spawn_object(o, MODEL_NONE, bhvSparkleSpawn);
            PlaySound(SOUND_ENV_STAR);
            if (o->oTimer == 30) {
                o->oAction = 2;
                o->oForwardVel = 0;
                play_power_star_jingle(TRUE);
            }
            break;

        case 2:
            if (o->oTimer < 20)
                o->oVelY = 20 - o->oTimer;
            else
                o->oVelY = -10.0f;

            spawn_object(o, MODEL_NONE, bhvSparkleSpawn);
            obj_move_xyz_using_fvel_and_yaw(o);
            o->oFaceAngleYaw = o->oFaceAngleYaw - o->oTimer * 0x10 + 0x1000;
            PlaySound(SOUND_ENV_STAR);

            if (o->oPosY < o->oHomeY) {
                PlaySound2(SOUND_GENERAL_STAR_APPEARS);
                obj_become_tangible();
                o->oPosY = o->oHomeY;
                o->oAction = 3;
            }
            break;

        case 3:
            o->oFaceAngleYaw += 0x800;
            if (o->oTimer == 20) {
                gObjCutsceneDone = TRUE;
                clear_time_stop_flags(TIME_STOP_ENABLED | TIME_STOP_MARIO_AND_DOORS);
                o->activeFlags &= ~0x20;
            }

            if (o->oInteractStatus & INT_STATUS_INTERACTED) {
                mark_obj_for_deletion(o);
                o->oInteractStatus = 0;
            }
            break;
    }
}

struct Object *func_802F1A50(struct Object *sp30, f32 sp34, f32 sp38, f32 sp3C) {
    sp30 = spawn_object_abs_with_rot(o, 0, MODEL_STAR, bhvStarSpawnCoordinates, o->oPosX, o->oPosY,
                                     o->oPosZ, 0, 0, 0);
    sp30->oBehParams = o->oBehParams;
    sp30->oHomeX = sp34;
    sp30->oHomeY = sp38;
    sp30->oHomeZ = sp3C;
    sp30->oFaceAnglePitch = 0;
    sp30->oFaceAngleRoll = 0;
    return sp30;
}

void create_star(f32 sp20, f32 sp24, f32 sp28) {
    struct Object *sp1C;
    sp1C = func_802F1A50(sp1C, sp20, sp24, sp28);
    sp1C->oBehParams2ndByte = 0;
}

void func_802F1B84(f32 sp20, f32 sp24, f32 sp28) {
    struct Object *sp1C;
    sp1C = func_802F1A50(sp1C, sp20, sp24, sp28);
    sp1C->oBehParams2ndByte = 1;
}

void func_802F1BD4(f32 sp20, f32 sp24, f32 sp28) {
    struct Object *sp1C;
    sp1C = func_802F1A50(sp1C, sp20, sp24, sp28);
    sp1C->oBehParams2ndByte = 1;
    sp1C->oInteractionSubtype |= INT_SUBTYPE_NO_EXIT;
}

void bhv_hidden_red_coin_star_init(void) {
    s16 sp36;
    struct Object *sp30;

    if (gCurrCourseNum != 3)
        spawn_object(o, MODEL_TRANSPARENT_STAR, bhvRedCoinStarMarker);

    sp36 = count_objects_with_behavior(bhvRedCoin);
    if (sp36 == 0) {
        // TODO: this should probably not be MODEL_STAR, idk.
        sp30 =
            spawn_object_abs_with_rot(o, 0, MODEL_STAR, bhvStar, o->oPosX, o->oPosY, o->oPosZ, 0, 0, 0);
        sp30->oBehParams = o->oBehParams;
        o->activeFlags = 0;
    }

    o->oHiddenStarTriggerCounter = 8 - sp36;
}

void bhv_hidden_red_coin_star_loop(void) {
    gRedCoinsCollected = o->oHiddenStarTriggerCounter;
    switch (o->oAction) {
        case 0:
            if (o->oHiddenStarTriggerCounter == 8)
                o->oAction = 1;
            break;

        case 1:
            if (o->oTimer > 2) {
                func_802F1B84(o->oPosX, o->oPosY, o->oPosZ);
                func_802A3004();
                o->activeFlags = 0;
            }
            break;
    }
}
