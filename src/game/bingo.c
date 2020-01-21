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

s32 gBingoInitialized = 0;
u32 gBingoInitialSeed = 0;

s64 gbGlobalBingoTimer = 0;
s32 gbBingoCompleted = 0;
s32 gbBingoShowTimer = 1;

s16 gbStarIndex = 0;

s32 gbCoinsJustGotten = 0;

struct BingoObjective gBingoObjectives[25];
enum BingoObjectiveUpdate sBingoCurrUpdate;

void set_objective_state(struct BingoObjective *objective, enum BingoObjectiveState state) {
    // Only play the corresponding sound once
    if (objective->state == state) {
        return;
    }

    switch (state) {
        case BINGO_STATE_COMPLETE:
            // SetSound(SOUND_CH8_UNK6A, D_803320E0);
            play_sound(SOUND_GENERAL2_RIGHT_ANSWER, gDefaultSoundArgs);
            break;
        case BINGO_STATE_FAILED_IN_THIS_COURSE:
            // SetSound(SOUND_MENU_CAMERABUZZ, D_803320E0);
            play_sound(SOUND_MENU_CAMERA_BUZZ, gDefaultSoundArgs);
            break;
    }
    objective->state = state;
}

u8 bingo_check_win() {
    u8 i, j;
    u8 buckets[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

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

    for (i = 0; i < 12; i++) {
        if (buckets[i] == 5) {
            return 1;
        }
    }

    return 0;
}

void bingo_update(enum BingoObjectiveUpdate update) {
    s32 i;
    sBingoCurrUpdate = update;

    // This is to avoid a bug where the call to bingo_update() from area.c
    // (the once-a-frame call) crashes before setup_bingo_objectives() has
    // been called.
    if (!gBingoInitialized) {
        return;
    }

    for (i = 0; i < 25; i++) {
        update_objective(&gBingoObjectives[i]);
    }

    if (update == BINGO_UPDATE_TIMER_FRAME && !gbBingoCompleted) {
        gbGlobalBingoTimer++;
    }

    // Timer updates can never result in bingo being won
    if (update != BINGO_UPDATE_TIMER_FRAME) {
        if (bingo_check_win()) {
            gbBingoCompleted = 1;
        }
    }
}
