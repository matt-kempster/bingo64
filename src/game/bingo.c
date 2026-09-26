#include <ultra64.h>
#include <PR/os_cont.h>
#include <PR/os_libc.h>

#include "types.h"
#include "game_init.h"
#include "sm64.h"
#include "print.h"
#include "hud.h"
#include "area.h"
#include "save_file.h"
#include "bingo.h"
#include "bingo_net.h"
#include "ingame_menu.h"
#include "menu/file_select.h"
#include "engine/behavior_script.h"
#include "level_update.h"
#include "sounds.h"
#include "audio/external.h"
#include "bingo_objective_func.h"
#include "splatoon.h"
#include "camera.h"
#include "object_helpers.h"
#include "behavior_data.h"

s32 gBingoInitialized = 0;
u32 gBingoInitialSeed = 0;

s64 gbGlobalBingoTimer = 0;
enum BingoGameMode gbBingoMode = BINGO_MODE_LINE_1;
s32 gbBingosCompleted = 0;
s32 gbBingoTimeout = 0;
u32 gBingoCellClaimers[25] = { 0 };
s32 gBingoCallsOpen = BINGO_CALLS_OPEN_DEFAULT;
s32 gBingoCallsToWin = BINGO_CALLS_TO_WIN_DEFAULT;
u8 gBingoCallQueue[25];
s32 gbBingoShowCongratsCounter = 0;
s32 gbBingoShowCongratsLimit = 2;
s32 gbBingoTimerDisabled = 0;
s32 gbBingoShowTimer = 1;
u32 gBingoSeed = 0;

s16 gbStarIndex = 0;
u8 gbStarFromCannon = 0;

s32 gbCoinsJustGotten = 0;

u8 gBingoFullGameUnlocked = 1;
u8 gBingoNonstop = 0;
s32 gBingoReverseJoystickActive = 0;
s32 gBingoRandomStarsActive = 0;
s32 gBingoClickGameActive = 0;
s32 gBingoClickCounter = 0;
s16 gBingoClickGamePrevCameraSettings = 0;
s32 gBingoClickGamePrevCameraIndex = 0;
s32 gBingoDaredevilActive = 0;
s32 gBingoDaredevilPrevHealth = 0;
s32 gStarSelectScreenActive = 0;

// Star Selector count models printed in the act selector menu.
enum BingoModifier gBingoStarSelected = BINGO_MODIFIER_NONE;

// Last-confirmed act selector choices, per course, so re-entering a level
// restores them. Act 0 means "no choice saved yet".
enum BingoModifier gBingoStickyModifier[COURSE_STAGES_COUNT] = { BINGO_MODIFIER_NONE };
s8 gBingoStickyActNum[COURSE_STAGES_COUNT] = { 0 };

struct BingoObjective gBingoObjectives[25];
u8 gBingoObjectivesDisabled[BINGO_OBJECTIVE_TOTAL_AMOUNT] = { 0 };

// Presets hold the toggles as BINGO_MASK_WORDS u64s (the online options
// message carries them as a hex string of any width).
STATIC_ASSERT(BINGO_OBJECTIVE_TOTAL_AMOUNT <= 64 * BINGO_MASK_WORDS, "objective mask too narrow");

s32 bingo_objective_needs_unlock_off(enum BingoObjectiveType type) {
    return BINGO_OBJECTIVE_PROGRESSION_MIN <= type && type <= BINGO_OBJECTIVE_PROGRESSION_MAX;
}

s32 bingo_objective_eligible(enum BingoObjectiveType type) {
    if (gBingoObjectivesDisabled[type]) {
        return 0;
    }
    return !(gBingoFullGameUnlocked && bingo_objective_needs_unlock_off(type));
}

// Mask words are built per word w: OBJ_BIT(name, w) is the type's bit if it
// lives in word w, else 0. (Shift counts are & 63 so the untaken branches
// stay in range.)
#define OBJ_BIT(name, w) \
    ((BINGO_OBJECTIVE_##name >> 6) == (w) ? (u64) 1 << (BINGO_OBJECTIVE_##name & 63) : 0)
#define ALL_OBJECTIVES(w)                                                  \
    (BINGO_OBJECTIVE_TOTAL_AMOUNT >= 64 * ((w) + 1) ? ~(u64) 0             \
     : BINGO_OBJECTIVE_TOTAL_AMOUNT <= 64 * (w)     ? 0                    \
     : ~(u64) 0 >> ((64 * ((w) + 1) - BINGO_OBJECTIVE_TOTAL_AMOUNT) & 63))

// SRL-style boards are star/coin/level goals in an unmodified game;
// modifiers, timers, and counter-grinding collectables are all off. Opening
// cannons and Toad stars are SRL goals (the preset plays unlock OFF, so
// they're dealt); MIPS is not an SRL goal. So are "N 100 Coin Stars" and
// "N Castle Secret Stars".
#define PRESET_SRL_ENABLED(w) \
    (OBJ_BIT(STAR, w) | OBJ_BIT(COIN, w) | OBJ_BIT(STARS_IN_LEVEL, w) | OBJ_BIT(BOWSER, w) \
     | OBJ_BIT(ROOF_WITHOUT_CANNON, w) | OBJ_BIT(RACING_STARS, w) | OBJ_BIT(SECRETS_STARS, w) \
     | OBJ_BIT(MULTICOIN, w) | OBJ_BIT(MULTISTAR, w) | OBJ_BIT(STARS_MULTIPLE_LEVELS, w) \
     | OBJ_BIT(RED_COIN, w) | OBJ_BIT(BLUE_COIN, w) | OBJ_BIT(RED_COIN_STARS, w) \
     | OBJ_BIT(OPEN_CANNONS, w) | OBJ_BIT(TOAD_STARS, w) | OBJ_BIT(HUNDRED_COIN_STARS, w) \
     | OBJ_BIT(CASTLE_SECRET_STARS, w))

// Everything that couldn't happen under vanilla rules: the game-modifying
// stars, splatoon, ordered reds, and forced-timer stars. (TTC Random stays:
// the clock setting is a vanilla mechanic.)
#define PRESET_VANILLA_DISABLED(w) \
    (OBJ_BIT(STAR_TIMED, w) | OBJ_BIT(STAR_CLICK_GAME, w) \
     | OBJ_BIT(STAR_REVERSE_JOYSTICK, w) | OBJ_BIT(STAR_GREEN_DEMON, w) \
     | OBJ_BIT(STAR_DAREDEVIL, w) | OBJ_BIT(RANDOM_RED_COINS, w) | OBJ_BIT(SPLATOON, w))

// Casual keeps the fun modifiers (daredevil, splatoon) but drops anything
// timed or execution-heavy (the coinless star rides with the button
// challenges: same restriction-star family).
#define PRESET_CASUAL_DISABLED(w) \
    (OBJ_BIT(STAR_TIMED, w) | OBJ_BIT(STAR_A_BUTTON_CHALLENGE, w) \
     | OBJ_BIT(STAR_B_BUTTON_CHALLENGE, w) | OBJ_BIT(STAR_Z_BUTTON_CHALLENGE, w) \
     | OBJ_BIT(STAR_COINLESS, w) \
     | OBJ_BIT(STAR_CLICK_GAME, w) | OBJ_BIT(STAR_REVERSE_JOYSTICK, w) \
     | OBJ_BIT(STAR_GREEN_DEMON, w) | OBJ_BIT(DANGEROUS_WALL_KICKS, w) \
     | OBJ_BIT(ROOF_WITHOUT_CANNON, w) | OBJ_BIT(BLJ, w))

// Rows in enum BingoPresetId order. SRL races start from a fresh file
// (unlock OFF) and play the modern head-to-head format (lockout).
const struct BingoPreset gBingoPresets[BINGO_PRESET_COUNT] = {
    { { ALL_OBJECTIVES(0) & ~PRESET_SRL_ENABLED(0), ALL_OBJECTIVES(1) & ~PRESET_SRL_ENABLED(1) },
      0, BINGO_MODE_LOCKOUT },
    { { PRESET_VANILLA_DISABLED(0), PRESET_VANILLA_DISABLED(1) }, 1, BINGO_MODE_LINE_1 },
    { { PRESET_CASUAL_DISABLED(0), PRESET_CASUAL_DISABLED(1) }, 1, BINGO_MODE_LINE_1 },
};

void bingo_preset_apply(enum BingoPresetId preset) {
    s32 i;
    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
        gBingoObjectivesDisabled[i] = (u8) BINGO_MASK_BIT(gBingoPresets[preset].objectivesDisabled, i);
    }
    gBingoFullGameUnlocked = gBingoPresets[preset].fullGameUnlocked;
    gbBingoMode = gBingoPresets[preset].mode;
}

s32 bingo_preset_current(void) {
    s32 p;
    s32 i;
    s32 same;
    for (p = 0; p < BINGO_PRESET_COUNT; p++) {
        same = 1;
        for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
            if (BINGO_MASK_BIT(gBingoPresets[p].objectivesDisabled, i) != (gBingoObjectivesDisabled[i] != 0)) {
                same = 0;
                break;
            }
        }
        if (same
            && gBingoPresets[p].fullGameUnlocked == (gBingoFullGameUnlocked != 0)
            && gBingoPresets[p].mode == gbBingoMode) {
            return p;
        }
    }
    return -1;
}


void disable_bingo_modifiers() {
    if (gBingoClickGameActive) {
        sSelectionFlags = gBingoClickGamePrevCameraSettings;
        gDialogCameraAngleIndex = gBingoClickGamePrevCameraIndex;
    }
    if (cur_obj_nearest_object_with_behavior(bhv1upGreenDemon) != NULL) {
        obj_mark_for_deletion(cur_obj_nearest_object_with_behavior(bhv1upGreenDemon));
    }
    gBingoReverseJoystickActive = 0;
    gBingoRandomStarsActive = 0;
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
            play_sound(SOUND_GENERAL2_RIGHT_ANSWER, gGlobalSoundSource);
            bingo_hud_update_state(objective->icon, BINGO_ICON_SUCCESS);
            bingo_net_on_local_complete(objective);
            break;
        case BINGO_STATE_FAILED_IN_THIS_COURSE:
            play_sound(SOUND_MENU_CAMERA_BUZZ, gGlobalSoundSource);
            bingo_hud_update_state(objective->icon, BINGO_ICON_FAILED);
            break;
    }
    objective->state = state;
}

s32 bingo_mode_exclusive(void) {
    return gbBingoMode == BINGO_MODE_LOCKOUT || gbBingoMode == BINGO_MODE_CALLS;
}

s32 bingo_exclusive_target(void) {
    return gbBingoMode == BINGO_MODE_CALLS ? gBingoCallsToWin : BINGO_LOCKOUT_TARGET;
}

// A cell's course, for spacing the call queue out; 0 = not tied to one.
// The star and per-course objectives all keep the course as the first
// member of their data (the unions are aligned, see are_duplicates).
static s32 bingo_objective_course(struct BingoObjective *objective) {
    enum BingoObjectiveType type = objective->type;
    if ((BINGO_OBJECTIVE_STAR_MIN <= type && type <= BINGO_OBJECTIVE_STAR_MAX)
        || type == BINGO_OBJECTIVE_RANDOM_STARS || type == BINGO_OBJECTIVE_COIN
        || type == BINGO_OBJECTIVE_1UPS_IN_LEVEL || type == BINGO_OBJECTIVE_STARS_IN_LEVEL
        || type == BINGO_OBJECTIVE_RANDOM_RED_COINS || type == BINGO_OBJECTIVE_SPLATOON) {
        return objective->data.starObjective.course;
    }
    return 0;
}

// Call order: easy cells first, then medium (and the center), then hard,
// shuffled within each tier; then, where the tier allows it, a call never
// shares a course with the two calls before it (those are the ones likely
// to be open next to it, and the last call's winner is standing there).
// Uses its own generator so the board's random stream is untouched.
void bingo_calls_build_queue(u32 seed) {
    u32 state = seed * 2654435761u + 0x9E3779B9u;
    s32 tier[25];
    s32 n = 0, t, i, j, k;
    for (t = 0; t < 3; t++) {
        s32 start = n;
        for (i = 0; i < 25; i++) {
            enum BingoObjectiveClass class = gBingoObjectives[i].class;
            s32 cellTier = class == BINGO_CLASS_EASY ? 0 : class == BINGO_CLASS_HARD ? 2 : 1;
            if (cellTier == t) {
                tier[n] = t;
                gBingoCallQueue[n++] = i;
            }
        }
        for (i = n - 1; i > start; i--) {
            u8 tmp;
            state = state * 1664525u + 1013904223u;
            j = start + (s32) ((state >> 8) % (u32) (i - start + 1));
            tmp = gBingoCallQueue[i];
            gBingoCallQueue[i] = gBingoCallQueue[j];
            gBingoCallQueue[j] = tmp;
        }
    }
    for (i = 1; i < 25; i++) {
        for (j = i; j < 25 && tier[j] == tier[i]; j++) {
            s32 c = bingo_objective_course(&gBingoObjectives[gBingoCallQueue[j]]);
            s32 clash = 0;
            for (k = i - 1; k >= 0 && k >= i - 2; k--) {
                clash |= c != 0 && c == bingo_objective_course(&gBingoObjectives[gBingoCallQueue[k]]);
            }
            if (!clash) {
                break;
            }
        }
        if (j < 25 && tier[j] == tier[i] && j != i) {
            // Pull the first non-clashing cell forward, keeping the rest
            // of the tier in order.
            u8 pick = gBingoCallQueue[j];
            for (k = j; k > i; k--) {
                gBingoCallQueue[k] = gBingoCallQueue[k - 1];
            }
            gBingoCallQueue[i] = pick;
        }
    }
}

u32 bingo_calls_open_mask(void) {
    u32 mask = 0;
    s32 i, open = 0;
    if (gbBingoMode != BINGO_MODE_CALLS) {
        return 0;
    }
    for (i = 0; i < 25 && open < gBingoCallsOpen; i++) {
        s32 cell = gBingoCallQueue[i];
        if (gBingoObjectives[cell].state != BINGO_STATE_COMPLETE && gBingoCellClaimers[cell] == 0) {
            mask |= (u32) 1 << cell;
            open++;
        }
    }
    return mask;
}

s32 bingo_cell_live(s32 cell) {
    if (gbBingoMode != BINGO_MODE_CALLS) {
        return 1;
    }
    return (bingo_calls_open_mask() >> cell) & 1;
}

s32 bingo_mode_line_target(void) {
    switch (gbBingoMode) {
        case BINGO_MODE_LINE_1:   return 1;
        case BINGO_MODE_LINE_2:   return 2;
        case BINGO_MODE_LINE_3:   return 3;
        case BINGO_MODE_BLACKOUT: return 12;
        default:                  return 0;  // LOCKOUT/CALLS: not line-based
    }
}

s32 bingo_complete_cell_count(void) {
    s32 i, count = 0;
    for (i = 0; i < 25; i++) {
        if (gBingoObjectives[i].state == BINGO_STATE_COMPLETE) {
            count++;
        }
    }
    return count;
}

s32 bingo_race_won(void) {
    if (bingo_mode_exclusive()) {
        if (bingo_net_racing()) {
            // Online lockout/calls: claims are exclusive and the server
            // decides the winner (first to the target, or an uncatchable
            // lead).
            return bingo_net_local_won();
        }
        if (bingo_net_dropped()) {
            // An online race whose connection died: the board still
            // holds everyone's squares, so counting all complete cells
            // would hand us our opponents' progress. Only ours count.
            return bingo_net_local_cell_count() >= bingo_exclusive_target();
        }
        // Solo: race to any 13 squares (lockout) or the call target.
        return bingo_complete_cell_count() >= bingo_exclusive_target();
    }
    return gbBingosCompleted >= bingo_mode_line_target();
}

s32 bingo_timeout_frames(void) {
    return gbBingoTimeout * 60 * 30;
}

s32 bingo_race_timed_out(void) {
    if (bingo_net_racing()) {
        // Online the relay owns the clock and announces expiry (T),
        // breaking ties; never call it locally off a drifting timer.
        return bingo_net_race_timed_out();
    }
    // Solo (or a dropped race): the local clock decides. A win at the
    // buzzer is still a win.
    return gbBingoTimeout > 0 && !bingo_race_won()
           && gbGlobalBingoTimer >= bingo_timeout_frames();
}

s32 bingo_race_over(void) {
    if (bingo_race_won()) {
        return 1;
    }
    if (bingo_race_timed_out()) {
        return 1;
    }
    // An online lockout decided for someone else ends the race for us too.
    return bingo_mode_exclusive() && bingo_net_race_decided();
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
    u32 live;
    // This is to avoid a bug where the call to bingo_update() from area.c
    // (the once-a-frame call) crashes before setup_bingo_objectives() has
    // been called.
    if (!gBingoInitialized) {
        return;
    }

    // The lives objective tracks the HUD's life counter. Watch it on the
    // frame tick and re-enter with a real event when it moves, because
    // timer updates themselves are excluded from the win check below.
    if (update == BINGO_UPDATE_TIMER_FRAME_GLOBAL) {
        static s16 sPrevLives = 0;
        if (gHudDisplay.lives != sPrevLives) {
            sPrevLives = gHudDisplay.lives;
            bingo_update(BINGO_UPDATE_LIVES);
        }
    }

    // Call and Response: only the open calls see events, so progress
    // counts from the call. Snapshot the open set first, or a call that
    // opens mid-loop would also collect the event that closed its
    // predecessor (one star, two calls).
    live = gbBingoMode == BINGO_MODE_CALLS ? bingo_calls_open_mask() : 0x1FFFFFF;
    for (i = 0; i < 25; i++) {
        if ((live >> i) & 1) {
            update_objective(&gBingoObjectives[i], update);
        }
    }

    if (update == BINGO_UPDATE_TIMER_FRAME_GLOBAL && !bingo_race_over()) {
        // Should we not increment if game is paused?
        // We probably should. Just a thought.
        gbGlobalBingoTimer++;
    }
    if (update == BINGO_UPDATE_COURSE_CHANGED) {
        disable_bingo_modifiers();

        // Put this here because, long story short, the bingo modifier
        // string renders before the game renders the stars; the star-rendering
        // is what resets this back to NONE otherwise. (On re-entry, the act
        // selector restores the course's sticky choice from
        // gBingoStickyModifier.)
        gBingoStarSelected = BINGO_MODIFIER_NONE;
    }

    // Timer updates can never result in bingo being won
    if (update != BINGO_UPDATE_TIMER_FRAME_GLOBAL && update != BINGO_UPDATE_TIMER_FRAME_STAR) {
        gbBingosCompleted = bingo_check_win();
    }
}
