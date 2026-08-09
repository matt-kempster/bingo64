/**
 * This file contains the initialization and behavior for red coins.
 * Behavior controls audio and the orange number spawned, as well as interacting with
 * the course's red coin star.
 */
#include "game/bingo.h"
#include "game/bingo_tracking_collectables.h"
// #include "menu/file_select.h"
#include "engine/rand.h"

/**
 * Red coin's hitbox details.
 */
static struct ObjectHitbox sRedCoinHitbox = {
    /* interactType:      */ INTERACT_COIN,
    /* downOffset:        */ 0,
    /* damageOrCoinValue: */ 2,
    /* health:            */ 0,
    /* numLootCoins:      */ 0,
    /* radius:            */ 100,
    /* height:            */ 64,
    /* hurtboxRadius:     */ 0,
    /* hurtboxHeight:     */ 0,
};

/**
 * Red coin initialization function. Sets the coin's hitbox and parent object.
 */
void bhv_red_coin_init(void) {
    // This floor and floor height are unused. Perhaps for orange number spawns originally?
    struct Surface *dummyFloor;
    UNUSED f32 floorHeight = find_floor(o->oPosX, o->oPosY, o->oPosZ, &dummyFloor);

    struct Object *hiddenRedCoinStar;

    s32 initialNum, targetNum = 0;
    int permutation[] = {0, 1, 2, 3, 4, 5, 6, 7};
    s32 i, j, temp;

    // Set the red coins to have a parent of the closest red coin star.
    hiddenRedCoinStar = cur_obj_nearest_object_with_behavior(bhvHiddenRedCoinStar);
    if (hiddenRedCoinStar != NULL)
        o->parentObj = hiddenRedCoinStar;
    else {
        hiddenRedCoinStar = cur_obj_nearest_object_with_behavior(bhvBowserCourseRedCoinStar);
        if (hiddenRedCoinStar != NULL) {
            o->parentObj = hiddenRedCoinStar;
        } else {
            o->parentObj = NULL;
        }
    }

    obj_set_hitbox(o, &sRedCoinHitbox);
    o->oBingoId = get_unique_id(BINGO_UPDATE_RED_COIN, o->oPosX, o->oPosY, o->oPosZ);

    if (gBingoStarSelected == BINGO_MODIFIER_ORDERED_RED_COINS) {
        genrand_push();
        // Seed RNG with bingo seed + level
        init_genrand(gBingoSeed + gCurrLevelNum);
        // Perform Fisher-Yates shuffle based on bingoID
        initialNum = (o->oBingoId % 8);
        for (i = 7; i > 0; i--) {
            j = genrand_int32() % (i + 1);
            temp = permutation[i];
            permutation[i] = permutation[j];
            permutation[j] = temp;
        }
        targetNum = permutation[initialNum] + 1;
        // Spawn orange number
        spawn_bingo_orange_number(targetNum, 0, 150, 0);
        genrand_pop();
    }
}

/**
 * Main behavior for red coins. Primarily controls coin collection noise and spawning
 * the orange number counter.
 */
void bhv_red_coin_loop(void) {
    struct Object *bingoNumber = NULL;
    // If Mario interacted with the object...
    if (o->oInteractStatus & INT_STATUS_INTERACTED) {
        // ...and there is a red coin star in the level...
        if (o->parentObj != NULL) {
            // ...increment the star's counter.
            o->parentObj->oHiddenStarTriggerCounter++;

            // For JP version, play an identical sound for all coins.
#ifdef VERSION_JP
            create_sound_spawner(SOUND_GENERAL_RED_COIN);
#endif
            // Spawn the orange number counter, as long as it isn't the last coin.
            if (o->parentObj->oHiddenStarTriggerCounter != 8) {
                spawn_orange_number(o->parentObj->oHiddenStarTriggerCounter, 0, 0, 0);
            }

            // On all versions but the JP version, each coin collected plays a higher noise.
#ifndef VERSION_JP
            play_sound(SOUND_MENU_COLLECT_RED_COIN
                           + (((u8) o->parentObj->oHiddenStarTriggerCounter - 1) << 16),
                       gDefaultSoundArgs);
#endif
        }

        coin_collected();
        // Despawn the coin.
        o->oInteractStatus = 0;
        bingoNumber = obj_nearest_object_with_behavior(bhvBingoOrangeNumber);
        if (bingoNumber != NULL) {
            if (o->parentObj->oHiddenStarTriggerCounter != bingoNumber->oBehParams2ndByte) {
                bingo_update(BINGO_UPDATE_WRONG_RED_COIN);
            }
            bingoNumber->activeFlags = 0;
        }
        // Tell Bingo
        if (is_new_kill(BINGO_UPDATE_RED_COIN, o->oBingoId)) {
            bingo_update(BINGO_UPDATE_RED_COIN);
        }
    }
}
