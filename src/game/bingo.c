#include <ultra64.h>
#include <PR/os_cont.h>
#include <PR/os_libc.h>

#include "types.h"
#include "game.h"
#include "sm64.h"
#include "print.h"
#include "hud.h"
#include "area.h"
#include "save_file.h"
#include "bingo.h"
#include "ingame_menu.h"
#include "menu/file_select.h"
#include "engine/behavior_script.h"
#include "level_update.h"
#include "audio_defines.h"
#include "audio/external.h"
#include "bingo_objective_func.h"
#include "splatoon.h"
#include "camera.h"
#include "object_helpers.h"
#include "behavior_data.h"

s32 gBingoInitialized = 0;
u32 gBingoInitialSeed = 0;

s64 gbGlobalBingoTimer = 0;
s32 gbBingoTarget = 1;
s32 gbBingosCompleted = 0;
s32 gbBingoShowCongratsCounter = 0;
s32 gbBingoShowCongratsLimit = 2;
s32 gbBingoTimerDisabled = 0;
s32 gbBingoShowTimer = 1;
u32 gBingoSeed = 0;

s16 gbStarIndex = 0;

s32 gbCoinsJustGotten = 0;

u8 gBingoFullGameUnlocked = 1;
s32 gBingoReverseJoystickActive = 0;
s32 gBingoClickGameActive = 0;
s32 gBingoClickCounter = 0;
s16 gBingoClickGamePrevCameraSettings = 0;
s32 gBingoClickGamePrevCameraIndex = 0;
s32 gBingoDaredevilActive = 0;
s32 gBingoDaredevilPrevHealth = 0;
s32 gStarSelectScreenActive = 0;

// Star Selector count models printed in the act selector menu.
enum BingoModifier gBingoStarSelected = BINGO_MODIFIER_NONE;

struct BingoObjective gBingoObjectives[25];
u8 gBingoObjectivesDisabled[BINGO_OBJECTIVE_TOTAL_AMOUNT] = { 0 };


void disable_bingo_modifiers() {
    if (gBingoClickGameActive) {
        sSelectionFlags = gBingoClickGamePrevCameraSettings;
        gDialogCameraAngleIndex = gBingoClickGamePrevCameraIndex;
    }
    if (obj_nearest_object_with_behavior(bhv1upGreenDemon) != NULL) {
        mark_object_for_deletion(obj_nearest_object_with_behavior(bhv1upGreenDemon));
    }
    gBingoReverseJoystickActive = 0;
    gBingoDaredevilActive = 0;
    gBingoClickGameActive = 0;
    gBingoClickCounter = -1;
    gSplatoonEnabled = 0;
    splatoon_clear();
}

void set_objective_state(struct BingoObjective *objective, enum BingoObjectiveState state) {
    // Only play the corresponding sound once
    if (objective->state == state) {
        return;
    }

    switch (state) {
        case BINGO_STATE_COMPLETE:
            play_sound(SOUND_GENERAL2_RIGHT_ANSWER, gDefaultSoundArgs);
            bingo_hud_update_state(objective->icon, BINGO_ICON_SUCCESS);
            break;
        case BINGO_STATE_FAILED_IN_THIS_COURSE:
            play_sound(SOUND_MENU_CAMERA_BUZZ, gDefaultSoundArgs);
            bingo_hud_update_state(objective->icon, BINGO_ICON_FAILED);
            break;
    }
    objective->state = state;
}

/**
 * Return number of bingos on the board.
 */
u8 bingo_check_win() {
    u8 i, j;
    u8 buckets[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    u8 bingos;

    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            if (gBingoObjectives[i + j * 5].state == BINGO_STATE_COMPLETE) {
                buckets[i]++;
                buckets[j + 5]++;
                if (i == j) {
                    buckets[10]++;
                }
                if (i == 4 - j) {
                    buckets[11]++;
                }
            }
        }
    }

    bingos = 0;
    for (i = 0; i < 12; i++) {
        if (buckets[i] == 5) {
            bingos += 1;
        }
    }

    return bingos;
}

void bingo_track_death(u32 deathAction) {
    enum BingoObjectiveUpdate update;

    switch (deathAction) {
        case ACT_STANDING_DEATH:
            update = BINGO_UPDATE_DEATH_STANDING;
            break;
        case ACT_DEATH_ON_BACK:
            update = BINGO_UPDATE_DEATH_ON_BACK;
            break;
        case ACT_DEATH_ON_STOMACH:
            update = BINGO_UPDATE_DEATH_ON_STOMACH;
            break;
        case ACT_DROWNING:
            update = BINGO_UPDATE_DEATH_DROWNED;
            break;
        case ACT_WATER_DEATH:
            update = BINGO_UPDATE_DEATH_IN_WATER;
            break;
        case ACT_QUICKSAND_DEATH:
            update = BINGO_UPDATE_DEATH_QUICKSAND;
            break;
        case ACT_ELECTROCUTION:
            update = BINGO_UPDATE_DEATH_SHOCKED;
            break;
        case ACT_SUFFOCATION:
            update = BINGO_UPDATE_DEATH_GAS;
            break;
        case ACT_EATEN_BY_BUBBA:
            update = BINGO_UPDATE_DEATH_EATEN;
            break;
        case ACT_SQUISHED:
            update = BINGO_UPDATE_DEATH_SQUISHED;
            break;
        case ACT_LAVA_BOOST:
            update = BINGO_UPDATE_DEATH_LAVA;
            break;
        case ACT_CAUGHT_IN_WHIRLPOOL:
            update = BINGO_UPDATE_DEATH_WHIRLPOOL;
            break;
        default:
            // Only deaths reach here without a death action: falling
            // out of the level (m->floor == NULL in update_mario_inputs).
            update = BINGO_UPDATE_DEATH_FELL_OUT;
            break;
    }
    bingo_update(update);
}

void bingo_update(enum BingoObjectiveUpdate update) {
    s32 i;
    // This is to avoid a bug where the call to bingo_update() from area.c
    // (the once-a-frame call) crashes before setup_bingo_objectives() has
    // been called.
    if (!gBingoInitialized) {
        return;
    }

    for (i = 0; i < 25; i++) {
        update_objective(&gBingoObjectives[i], update);
    }

    if (update == BINGO_UPDATE_TIMER_FRAME_GLOBAL && gbBingosCompleted < gbBingoTarget) {
        // Should we not increment if game is paused?
        // We probably should. Just a thought.
        gbGlobalBingoTimer++;
    }
    if (update == BINGO_UPDATE_COURSE_CHANGED) {
        disable_bingo_modifiers();

        // Put this here because, long story short, the bingo modifier
        // string renders before the game renders the stars; the star-rendering
        // is what resets this back to NONE otherwise.
        // TODO: be intelligent about retries
        gBingoStarSelected = BINGO_MODIFIER_NONE;
    }

    // Timer updates can never result in bingo being won
    if (update != BINGO_UPDATE_TIMER_FRAME_GLOBAL && update != BINGO_UPDATE_TIMER_FRAME_STAR) {
        gbBingosCompleted = bingo_check_win();
    }
}
