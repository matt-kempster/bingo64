#include <ultra64.h>
#include <PR/os_libc.h>

#include "area.h"
#include "bingo.h"

#include "engine/behavior_script.h"
#include "engine/math_util.h"
#include "engine/rand.h"
#include "bingo_descriptions.h"
#include "bingo_const.h"
#include "bingo_objective_func.h"
#include "bingo_objective_init.h"
#include "bingo_tracking_collectables.h"
#include "bingo_tracking_star.h"
#include "segment2.h"
#include "strcpy.h"
#include "print.h"

struct ObjectiveWeight {
    enum BingoObjectiveType objective;
    s32 weight;
    s32 usesRemaining;  // -1 if infinite
};

#define NO_LIMIT -1

struct ObjectiveWeight sWeightsEasy[] = {
    { BINGO_OBJECTIVE_COIN, 12, 2 },
    { BINGO_OBJECTIVE_SPLATOON, 8, 1 },
    { BINGO_OBJECTIVE_STAR, 12, 3 },
    { BINGO_OBJECTIVE_LOSE_MARIO_HAT, 12, 1 },
    { BINGO_OBJECTIVE_UNIQUE_DEATHS, 8, 1 },
    { BINGO_OBJECTIVE_BLJ, 12, 1 },
    { BINGO_OBJECTIVE_RACING_STARS, 12, 1 },
    { BINGO_OBJECTIVE_MULTISTAR, 6, 1 },
    { BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS, 6, 1 },
};
s32 sWeightsSizeEasy = sizeof(sWeightsEasy) / sizeof(struct ObjectiveWeight);

struct ObjectiveWeight sWeightsMedium[] = {
    { BINGO_OBJECTIVE_COIN, 12, NO_LIMIT },
    { BINGO_OBJECTIVE_SPLATOON, 8, 2 },
    { BINGO_OBJECTIVE_STAR, 6, 2 },
    { BINGO_OBJECTIVE_KILL_GOOMBAS, 6, 2 },
    { BINGO_OBJECTIVE_KILL_BOBOMBS, 6, 2 },
    { BINGO_OBJECTIVE_KILL_SPINDRIFTS, 6, 1 },
    { BINGO_OBJECTIVE_KILL_MR_IS, 6, 1 },
    { BINGO_OBJECTIVE_KILL_SCUTTLEBUGS, 6, 1 },
    { BINGO_OBJECTIVE_KILL_BULLIES, 6, 1 },
    { BINGO_OBJECTIVE_KILL_CHUCKYAS, 6, 1 },
    { BINGO_OBJECTIVE_AMPS, 6, 1 },
    { BINGO_OBJECTIVE_STAR_TIMED, 12, 3 },
    { BINGO_OBJECTIVE_STAR_TTC_RANDOM, 8, 2 },
    { BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE, 3, 3 },
    { BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE, 3, 3 },
    { BINGO_OBJECTIVE_STAR_DAREDEVIL, 12, 3 },
    { BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK, 8, 2 },
    { BINGO_OBJECTIVE_STAR_CLICK_GAME, 8, 2 },
    { BINGO_OBJECTIVE_RANDOM_RED_COINS, 12, 3 },
    { BINGO_OBJECTIVE_1UPS_IN_LEVEL, 12, NO_LIMIT },
    { BINGO_OBJECTIVE_UNIQUE_DEATHS, 8, 1 },
    { BINGO_OBJECTIVE_SIGNPOST, 12, 2 },
    { BINGO_OBJECTIVE_SHOOT_CANNONS, 12, 2 },
    { BINGO_OBJECTIVE_RED_COIN, 12, 2 },
    { BINGO_OBJECTIVE_EXCLAMATION_MARK_BOX, 8, 2 },
    { BINGO_OBJECTIVE_SECRETS_STARS, 8, 1 },
    { BINGO_OBJECTIVE_RACING_STARS, 4, 1 },
    { BINGO_OBJECTIVE_WING_CAP_BOX, 4, 2 },
    { BINGO_OBJECTIVE_VANISH_CAP_BOX, 4, 2 },
    { BINGO_OBJECTIVE_METAL_CAP_BOX, 4, 2 },
    { BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS, 12, 1 },
    { BINGO_OBJECTIVE_MULTISTAR, 6, NO_LIMIT },
    { BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS, 6, 1 },
    { BINGO_OBJECTIVE_BOWSER, 4, 1 },
    { BINGO_OBJECTIVE_ROOF_WITHOUT_CANNON, 2, 1 },
};
s32 sWeightsSizeMedium = sizeof(sWeightsMedium) / sizeof(struct ObjectiveWeight);

struct ObjectiveWeight sWeightsHard[] = {
    { BINGO_OBJECTIVE_STAR_TIMED, 12, 1 },
    { BINGO_OBJECTIVE_SPLATOON, 8, 2 },
    { BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE, 12, 2 },
    { BINGO_OBJECTIVE_1UPS_IN_LEVEL, 12, 1 },
    { BINGO_OBJECTIVE_STARS_IN_LEVEL, 12, NO_LIMIT },
    { BINGO_OBJECTIVE_MULTICOIN, 4, NO_LIMIT },
    { BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK, 16, NO_LIMIT },
    { BINGO_OBJECTIVE_STAR_CLICK_GAME, 8, NO_LIMIT },
    { BINGO_OBJECTIVE_STAR_GREEN_DEMON, 12, NO_LIMIT },
    { BINGO_OBJECTIVE_STAR_DAREDEVIL, 8, 3 },
    { BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS, 12, 1 },
    { BINGO_OBJECTIVE_POLES, 12, 2 },
    { BINGO_OBJECTIVE_SHOOT_CANNONS, 12, 1 },
    { BINGO_OBJECTIVE_RED_COIN, 12, 1 },
    { BINGO_OBJECTIVE_AMPS, 6, 1 },
    { BINGO_OBJECTIVE_KILL_BULLIES, 6, 1 },
    { BINGO_OBJECTIVE_KILL_CHUCKYAS, 6, 1 },
    { BINGO_OBJECTIVE_SIGNPOST, 12, 1 },
    { BINGO_OBJECTIVE_MULTISTAR, 6, 1 },
    { BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS, 6, 1 },
};
s32 sWeightsSizeHard = sizeof(sWeightsHard) / sizeof(struct ObjectiveWeight);

struct ObjectiveWeight sWeightsCenter[] = {
    { BINGO_OBJECTIVE_COIN, 12, NO_LIMIT },
    { BINGO_OBJECTIVE_KILL_GOOMBAS, 6, 1 },
    { BINGO_OBJECTIVE_KILL_BOBOMBS, 6, 1 },
    { BINGO_OBJECTIVE_MULTICOIN, 12, NO_LIMIT },
    { BINGO_OBJECTIVE_MULTISTAR, 6, 1 },
    { BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS, 6, 1 },
    { BINGO_OBJECTIVE_POLES, 3, 1 },
    { BINGO_OBJECTIVE_SHOOT_CANNONS, 3, 1 },
    { BINGO_OBJECTIVE_AMPS, 3, 1 },
    { BINGO_OBJECTIVE_BOWSER, 3, 1 },
};
s32 sWeightsSizeCenter = sizeof(sWeightsCenter) / sizeof(struct ObjectiveWeight);

// The budgets above (usesRemaining) are consumed as a board is dealt, so
// they must start fresh for every board. Regenerating in-process — netplay
// rematches, new save files — otherwise drains them until a class's weight
// sum reaches 0 and get_random_objective_type faults on `% sum`.
static void reset_weight_budgets(void) {
    static s32 sFreshUses[128];  // >= total entries across the four tables
    static s32 sFreshSaved = 0;
    struct ObjectiveWeight *tables[4];
    s32 sizes[4];
    s32 t, i, k;

    tables[0] = sWeightsEasy;   sizes[0] = sWeightsSizeEasy;
    tables[1] = sWeightsMedium; sizes[1] = sWeightsSizeMedium;
    tables[2] = sWeightsHard;   sizes[2] = sWeightsSizeHard;
    tables[3] = sWeightsCenter; sizes[3] = sWeightsSizeCenter;
    k = 0;
    for (t = 0; t < 4; t++) {
        for (i = 0; i < sizes[t]; i++, k++) {
            if (!sFreshSaved) {
                sFreshUses[k] = tables[t][i].usesRemaining;
            } else {
                tables[t][i].usesRemaining = sFreshUses[k];
            }
        }
    }
    sFreshSaved = 1;
}

struct ObjectiveWeight *find_weight(
    enum BingoObjectiveClass class,
    enum BingoObjectiveType objective
) {
    struct ObjectiveWeight *weights;
    s32 size;
    s32 i;
    switch (class) {
        case BINGO_CLASS_EASY:
            weights = sWeightsEasy;
            size = sWeightsSizeEasy;
            break;
        case BINGO_CLASS_MEDIUM:
            weights = sWeightsMedium;
            size = sWeightsSizeMedium;
            break;
        case BINGO_CLASS_HARD:
            weights = sWeightsHard;
            size = sWeightsSizeHard;
            break;
        case BINGO_CLASS_CENTER:
            weights = sWeightsCenter;
            size = sWeightsSizeCenter;
            break;
    }
    for (i = 0; i < size; i++) {
        if (weights[i].objective == objective) {
            return &weights[i];
        }
    }
    return NULL;
}

struct ObjectiveWeight *get_random_objective_type(enum BingoObjectiveClass class) {
    struct ObjectiveWeight *weights;
    s32 size;
	s32 sum;
	s32 want_sum;
	s32 i;

    switch (class) {
        case BINGO_CLASS_EASY:
            weights = sWeightsEasy;
            size = sWeightsSizeEasy;
            break;
        case BINGO_CLASS_MEDIUM:
            weights = sWeightsMedium;
            size = sWeightsSizeMedium;
            break;
        case BINGO_CLASS_HARD:
            weights = sWeightsHard;
            size = sWeightsSizeHard;
            break;
        case BINGO_CLASS_CENTER:
            weights = sWeightsCenter;
            size = sWeightsSizeCenter;
            break;
    }
    sum = 0.0f;
    for (i = 0; i < size; i++) {
        if (weights[i].usesRemaining != 0) {
            sum += weights[i].weight;
        }
    }
    // TODO: if sum is 0, choose completely randomly
    // (while initializing usesRemaining given disabled objs)
    want_sum = random_u16() % sum;

    i = -1;
    sum = 0;
    do {
        i++;
        if (weights[i].usesRemaining != 0) {
            sum += weights[i].weight;
        }
    } while (sum < want_sum);

    return &weights[i];
}

enum BingoObjectiveType get_random_enabled_objective_type(enum BingoObjectiveClass class) {
    u32 attempts = 10;
    struct ObjectiveWeight *candidate;
    enum BingoObjectiveType i;
    s32 enabledSum = 0;
    u32 randomIndex;
    u32 enabledCounter = 0;

    while (attempts > 0) {
        candidate = get_random_objective_type(class);
        if (!gBingoObjectivesDisabled[candidate->objective]) {
            if (candidate->usesRemaining != NO_LIMIT) {
                candidate->usesRemaining--;
            }
            return candidate->objective;
        }
        attempts--;
    }
    // Tried a few times; just give up and get a completely random objective
    for (i = BINGO_OBJECTIVE_TYPE_MIN; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
        if (!gBingoObjectivesDisabled[i]) {
            enabledSum++;
        }
    }
    if (enabledSum == 0) {
        // All objectives are disabled. I guess just allow free play?
        return;
    }
    randomIndex = (random_u16() % enabledSum) + 1;
    for (i = BINGO_OBJECTIVE_TYPE_MIN; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
        if (!gBingoObjectivesDisabled[i]) {
            enabledCounter++;
        }
        if (enabledCounter == randomIndex) {
            return i;
        }
    }
    // We shouldn't get here.
}

s32 switch_to(s32 exclude) {
    s32 otherOne, otherTwo;
    s32 switchTo;
    switch (exclude) {
        case 0:
            otherOne = 1;
            otherTwo = 2;
            break;
        case 1:
            otherOne = 0;
            otherTwo = 2;
            break;
        case 2:
            otherOne = 1;
            otherTwo = 2;
            break;
        case 3:
            otherOne = 3;  // remain hard
            otherTwo = 1;  // easy or harder
    }
    switch (random_u16() % 5) {
        case 0:
        case 1:
            switchTo = otherOne;
            break;
        case 2:
        case 3:
            switchTo = otherTwo;
            break;
        case 4:
            switchTo = 3;
            break;
    }
    return switchTo;
}

void shuffle(s32 *array, s32 length) {
    s32 i, j, choice;
    for (i = 0; i < length - 1; i++)
    {
        j = i + random_u16() / (65535 / (length - i) + 1);
        choice = array[j];
        array[j] = array[i];
        array[i] = choice;
    }
}

s32 are_duplicates(struct BingoObjective *obj1, struct BingoObjective *obj2) {
    enum BingoObjectiveType type1, type2;

    type1 = obj1->type;
    type2 = obj2->type;
    if (type1 == type2) {
        if (
            (BINGO_OBJECTIVE_COLLECTABLE_MIN <= type1 && type1 <= BINGO_OBJECTIVE_COLLECTABLE_MAX)
            || type1 == BINGO_OBJECTIVE_LOSE_MARIO_HAT
            || type1 == BINGO_OBJECTIVE_BOWSER
            || type1 == BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS
            || type1 == BINGO_OBJECTIVE_ROOF_WITHOUT_CANNON
            || type1 == BINGO_OBJECTIVE_RACING_STARS
            || type1 == BINGO_OBJECTIVE_SECRETS_STARS
        ) {
            return 1;
        }
        // There are several "course collectible" objectives that shouldn't be
        // duplicated at all
        if (
            (
                type1 == BINGO_OBJECTIVE_COIN
                || type1 == BINGO_OBJECTIVE_1UPS_IN_LEVEL
                || type1 == BINGO_OBJECTIVE_STARS_IN_LEVEL
                || type1 == BINGO_OBJECTIVE_RANDOM_RED_COINS
                || type1 == BINGO_OBJECTIVE_SPLATOON
            )
            && (
                obj1->data.courseCollectableData.course
                == obj2->data.courseCollectableData.course
            )
        ) {
            return 1;
        }
    }
    // Two identical star objectives are duplicates.
    if (
        BINGO_OBJECTIVE_STAR_MIN <= type1 && type1 <= BINGO_OBJECTIVE_STAR_MAX
        && BINGO_OBJECTIVE_STAR_MIN <= type2 && type2 <= BINGO_OBJECTIVE_STAR_MAX
        && obj1->data.starObjective.course == obj2->data.starObjective.course
        && obj1->data.starObjective.starIndex == obj2->data.starObjective.starIndex
    ) {
        return 1;
    }
    // (This union stuff is scary, but safe, because the unions are aligned in
    // their member positions...)
    if (
        (BINGO_OBJECTIVE_STAR_NO_DUPLICATES_MIN <= type1 && type1 <= BINGO_OBJECTIVE_STAR_NO_DUPLICATES_MAX)
        || (BINGO_OBJECTIVE_STAR_NO_DUPLICATES_MIN <= type2 && type2 <= BINGO_OBJECTIVE_STAR_NO_DUPLICATES_MAX)
    ) {
        // If one is a star objective without a modifier and the other is "get all the
        // stars in this course" and the courses match, they're duplicates:
        if (
            (type1 == BINGO_OBJECTIVE_STARS_IN_LEVEL || type2 == BINGO_OBJECTIVE_STARS_IN_LEVEL)
            && obj1->data.starObjective.course == obj2->data.starObjective.course
        ) {
            return 1;
        }
    }
    // The "multistar" objective, when less than or equal to the "stars in level"
    // objective, is duplicative.
    // TODO: Add up the rest of the row's stars.
    if (
        (
            type1 == BINGO_OBJECTIVE_STARS_IN_LEVEL && type2 == BINGO_OBJECTIVE_MULTISTAR
            && (obj2->data.collectableData.toGet <= obj1->data.courseCollectableData.toGet)
        )
        || (
            type2 == BINGO_OBJECTIVE_STARS_IN_LEVEL && type1 == BINGO_OBJECTIVE_MULTISTAR
            && (obj1->data.collectableData.toGet <= obj2->data.courseCollectableData.toGet)
        )
    ) {
        return 1;
    }
    return 0;
}

void replace_objective(struct BingoObjective *obj) {
    struct ObjectiveWeight *weight = find_weight(obj->class, obj->type);
    if (weight != NULL && weight->usesRemaining != NO_LIMIT) {
        weight->usesRemaining++;
    }
    bingo_objective_init(obj, obj->class, get_random_enabled_objective_type(obj->class));
}

s32 replace_one_if_duplicated(
    struct BingoObjective *obj1,
    struct BingoObjective *obj2
) {
    if (obj1->initialized && obj2->initialized && are_duplicates(obj1, obj2)) {
        if (random_u16() % 2 == 0) {
            replace_objective(obj1);
        } else {
            replace_objective(obj2);
        }
        return 1;
    }
    return 0;
}

s32 deduplicate_pass_single_bingo() {
    s32 row, col, row1, row2, col1, col2, diag1, diag2;
    struct BingoObjective *obj1;
    struct BingoObjective *obj2;
    enum BingoObjectiveType newtype;
    s32 anyReplaced = 0;

    for (row = 0; row < 5; row++) {
        for (col1 = 0; col1 < 5 - 1; col1++) {
            for (col2 = col1 + 1; col2 < 5; col2++) {
                obj1 = &gBingoObjectives[row * 5 + col1];
                obj2 = &gBingoObjectives[row * 5 + col2];
                anyReplaced |= replace_one_if_duplicated(obj1, obj2);
            }
        }
    }
    for (col = 0; col < 5; col++) {
        for (row1 = 0; row1 < 5 - 1; row1++) {
            for (row2 = row1 + 1; row2 < 5; row2++) {
                obj1 = &gBingoObjectives[row1 * 5 + col];
                obj2 = &gBingoObjectives[row2 * 5 + col];
                anyReplaced |= replace_one_if_duplicated(obj1, obj2);
            }
        }
    }
    for (diag1 = 0; diag1 < 5 - 1; diag1++) {
        for (diag2 = diag1 + 1; diag2 < 5; diag2++) {
            obj1 = &gBingoObjectives[diag1 * 5 + diag1];
            obj2 = &gBingoObjectives[diag2 * 5 + diag2];
            anyReplaced |= replace_one_if_duplicated(obj1, obj2);
            obj1 = &gBingoObjectives[diag1 * 5 + (4 - diag1)];
            obj2 = &gBingoObjectives[diag2 * 5 + (4 - diag2)];
            anyReplaced |= replace_one_if_duplicated(obj1, obj2);
        }
    }
    return anyReplaced;
}

s32 deduplicate_pass_multi_bingo() {
    s32 idx1, idx2;
    struct BingoObjective *obj1;
    struct BingoObjective *obj2;
    enum BingoObjectiveType newtype;
    s32 anyReplaced = 0;

    for (idx1 = 0; idx1 < 25 - 1; idx1++) {
        for (idx2 = idx1 + 1; idx2 < 25; idx2++) {
            obj1 = &gBingoObjectives[idx1];
            obj2 = &gBingoObjectives[idx2];
            anyReplaced |= replace_one_if_duplicated(obj1, obj2);
        }
    }
    return anyReplaced;
}

void deduplicate() {
    s32 attempts = 10;
    while (attempts > 0) {
        if (bingo_mode_line_target() == 1) {
            if (!deduplicate_pass_single_bingo()) {
                break;
            }
        } else {
            if (!deduplicate_pass_multi_bingo()) {
                break;
            }
        }
        attempts--;
    }
    // Give up or success, whatever
}

// Bingo setup and update hooks
void setup_bingo_objectives(u32 seed) {
    int row, col;
    s32 objectiveClasses[5][5] = {
        { 1, 0, 0, 0, 2 },
        { 0, 2, 0, 1, 0 },
        { 0, 0, 3, 0, 0 },
        { 0, 1, 0, 2, 0 },
        { 2, 0, 0, 0, 1 }
    };
    s32 indexOrder[25] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24
    };
    s32 harderClass, easierClass, class;
    enum BingoObjectiveClass classType;
    s32 i, index;
    enum BingoObjectiveType type;
    struct BingoObjective *objective;

    // Initialize random number subsystem
    init_genrand(seed);
    reset_weight_budgets();

    // A new board is a new race: clear every piece of session state a
    // previous race may have left behind (star/collectable tracking, the
    // race clock, sticky star-select acts), or objectives re-complete
    // from stale counts after EXIT GAME -> file select -> relaunch.
    bingo_tracking_star_reset();
    bingo_tracking_collectables_reset();
    gbGlobalBingoTimer = 0;
    gbBingosCompleted = 0;
    gbBingoShowCongratsCounter = 0;
    for (i = 0; i < COURSE_STAGES_COUNT; i++) {
        gBingoStickyActNum[i] = 0;
    }

    gBingoInitialized = 1;
    gBingoInitialSeed = seed;
    for (i = 0; i < 25; i++) {
        gBingoCellClaimers[i] = 0;
    }

    harderClass = (random_u16() % 2) + 1; // either 1 or 2
    easierClass = (2 - harderClass) + 1; // either 2 or 1, opposite harderClass

    // To shake it up a bit, mutate between 0 and 2 of the cells randomly.
    for (i = 0; i < random_u16() % 3; i++) {
        row = random_u16() % 5;
        col = random_u16() % 5;
        objectiveClasses[row][col] = switch_to(objectiveClasses[row][col]);
    }

    // NOTE!
    // We populate the bingo grid in random order because of restrictions on
    // amounts of duplicate objective types. If we were to populate the grid
    // in a fixed order every time, while also restricting that an objective
    // e.g. were to only appear once, then that objective would appear more
    // often in particular parts of the grid. We want "as random as possible"
    // hence this design choice.
    shuffle(&indexOrder, 25);

    for (i = 0; i < 25; i++) {
        index = indexOrder[i];
        class = objectiveClasses[index % 5][index / 5];
        objective = &gBingoObjectives[index];

        if (class == harderClass) {
            classType = BINGO_CLASS_HARD;
        } else if (class == easierClass) {
            classType = BINGO_CLASS_EASY;
        } else if (class == 3) {
            classType = BINGO_CLASS_CENTER;
        } else if (class == 0) {
            classType = BINGO_CLASS_MEDIUM;
        }
        type = get_random_enabled_objective_type(classType);
        bingo_objective_init(objective, classType, type);
    }

    // Not done yet: need to deduplicate rows/columns/diagonals.
    // For example if one objective is "kill 10 Goombas" and
    // another is "kill 11 Goombas", we want to remove one of them
    // and replace with another "non duplicate".
    deduplicate();
}

#undef NO_LIMIT
