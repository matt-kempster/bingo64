#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "area.h"
#include "bingo.h"
#include "engine/rand.h"
#include "harness.h"
#include "splatoon.h"

// From glue.c: records bingo_hud_update_number calls.
extern s32 gGlueHudNumberCalls;
extern s32 gGlueHudNumberLast;

void setup_bingo_objectives(u32 seed);
u8 bingo_check_win(void);
s32 are_duplicates(struct BingoObjective *obj1, struct BingoObjective *obj2);

// ---------------------------------------------------------------------------
// Board generation helpers.
//
// The board setup code keeps "how many times can this objective still be
// used" counters inside its weight tables, and they change while a board is
// made. To act like a fresh boot every time, we save the tables once at
// startup and put them back before each board we generate.

struct ObjectiveWeight {
    s32 objective;
    s32 weight;
    s32 usesRemaining;
};
extern struct ObjectiveWeight sWeightsEasy[], sWeightsMedium[], sWeightsHard[], sWeightsCenter[];
extern s32 sWeightsSizeEasy, sWeightsSizeMedium, sWeightsSizeHard, sWeightsSizeCenter;

#define MAX_WEIGHTS 64
static struct ObjectiveWeight sSavedEasy[MAX_WEIGHTS], sSavedMedium[MAX_WEIGHTS],
                              sSavedHard[MAX_WEIGHTS], sSavedCenter[MAX_WEIGHTS];
static int sWeightsSaved = 0;

static void save_or_restore_weights(void) {
    if (!sWeightsSaved) {
        memcpy(sSavedEasy, sWeightsEasy, sWeightsSizeEasy * sizeof(struct ObjectiveWeight));
        memcpy(sSavedMedium, sWeightsMedium, sWeightsSizeMedium * sizeof(struct ObjectiveWeight));
        memcpy(sSavedHard, sWeightsHard, sWeightsSizeHard * sizeof(struct ObjectiveWeight));
        memcpy(sSavedCenter, sWeightsCenter, sWeightsSizeCenter * sizeof(struct ObjectiveWeight));
        sWeightsSaved = 1;
    } else {
        memcpy(sWeightsEasy, sSavedEasy, sWeightsSizeEasy * sizeof(struct ObjectiveWeight));
        memcpy(sWeightsMedium, sSavedMedium, sWeightsSizeMedium * sizeof(struct ObjectiveWeight));
        memcpy(sWeightsHard, sSavedHard, sWeightsSizeHard * sizeof(struct ObjectiveWeight));
        memcpy(sWeightsCenter, sSavedCenter, sWeightsSizeCenter * sizeof(struct ObjectiveWeight));
    }
}

static void generate_board(u32 seed) {
    save_or_restore_weights();
    memset(gBingoObjectives, 0, sizeof(gBingoObjectives));
    gbBingoTarget = 1;
    gbBingosCompleted = 0;
    setup_bingo_objectives(seed);
}

static int is_star_type(enum BingoObjectiveType t) {
    return BINGO_OBJECTIVE_STAR_MIN <= t && t <= BINGO_OBJECTIVE_STAR_MAX;
}

// Writes one readable line per board cell. This doubles as our golden-file
// format and as a debugging aid.
static void dump_cell(FILE *out, int i) {
    struct BingoObjective *o = &gBingoObjectives[i];

    fprintf(out, "%02d type=%02d class=%d icon=%02d title=\"%s\"",
            i, o->type, o->class, o->icon, o->title);

    switch (o->type) {
        case BINGO_OBJECTIVE_STAR:
        case BINGO_OBJECTIVE_STAR_TTC_RANDOM:
        case BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK:
        case BINGO_OBJECTIVE_STAR_GREEN_DEMON:
        case BINGO_OBJECTIVE_STAR_DAREDEVIL:
            fprintf(out, " course=%d star=%d",
                    o->data.starObjective.course, o->data.starObjective.starIndex);
            break;
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
            fprintf(out, " course=%d star=%d hint=\"%s\"",
                    o->data.abcStarObjective.course, o->data.abcStarObjective.starIndex,
                    o->data.abcStarObjective.hint ? o->data.abcStarObjective.hint : "");
            break;
        case BINGO_OBJECTIVE_STAR_TIMED:
            fprintf(out, " course=%d star=%d maxTime=%d",
                    o->data.starTimerObjective.course, o->data.starTimerObjective.starIndex,
                    o->data.starTimerObjective.maxTime);
            break;
        case BINGO_OBJECTIVE_STAR_CLICK_GAME:
            fprintf(out, " course=%d star=%d maxClicks=%d",
                    o->data.starClicksObjective.course, o->data.starClicksObjective.starIndex,
                    o->data.starClicksObjective.maxClicks);
            break;
        case BINGO_OBJECTIVE_COIN:
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
        case BINGO_OBJECTIVE_RANDOM_RED_COINS:
        case BINGO_OBJECTIVE_SPLATOON:
            fprintf(out, " course=%d toGet=%d",
                    o->data.courseCollectableData.course, o->data.courseCollectableData.toGet);
            break;
        case BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS:
            fprintf(out, " toGetTotal=%d toGetEachCourse=%d",
                    o->data.multiCourseCollectableData.toGetTotal,
                    o->data.multiCourseCollectableData.toGetEachCourse);
            break;
        case BINGO_OBJECTIVE_BOWSER:
            fprintf(out, " level=%d", o->data.levelData.level);
            break;
        default:
            fprintf(out, " toGet=%d", o->data.collectableData.toGet);
            break;
    }
    fprintf(out, "\n");
}

static void dump_board(FILE *out) {
    int i;
    for (i = 0; i < 25; i++) {
        dump_cell(out, i);
    }
}

// ---------------------------------------------------------------------------
// RNG and basic determinism.

// Known first outputs of the reference MT19937 with seed 5489.
// If this passes, the host RNG behaves the same as the one in the ROM.
static void test_mt19937_reference(void) {
    unsigned long expected[5] = {
        3499211612UL, 581869302UL, 3890346734UL, 3586334585UL, 545404204UL
    };
    int i;
    init_genrand(5489);
    for (i = 0; i < 5; i++) {
        CHECK_EQ_INT(genrand_int32(), expected[i]);
    }
}

static void test_same_seed_same_board(void) {
    struct BingoObjective first[25];
    generate_board(12345);
    memcpy(first, gBingoObjectives, sizeof(first));
    generate_board(12345);
    CHECK(memcmp(first, gBingoObjectives, sizeof(first)) == 0);
}

static void test_different_seed_different_board(void) {
    struct BingoObjective first[25];
    generate_board(1);
    memcpy(first, gBingoObjectives, sizeof(first));
    generate_board(2);
    CHECK(memcmp(first, gBingoObjectives, sizeof(first)) != 0);
}

// ---------------------------------------------------------------------------
// Golden boards: the exact boards for a few fixed seeds, stored in
// golden/board_<seed>.txt. If board generation changes on purpose, run
// UPDATE_GOLDENS=1 make test and commit the new files.

static const u32 kGoldenSeeds[] = { 1, 12345, 314159 };

static void check_one_golden(u32 seed) {
    char path[64];
    char generated[8192];
    char stored[8192];
    size_t n;
    FILE *mem, *f;

    generate_board(seed);
    mem = fmemopen(generated, sizeof(generated) - 1, "w");
    dump_board(mem);
    fclose(mem);

    snprintf(path, sizeof(path), "golden/board_%u.txt", seed);

    if (getenv("UPDATE_GOLDENS")) {
        f = fopen(path, "w");
        fputs(generated, f);
        fclose(f);
        printf("  wrote %s\n", path);
        return;
    }

    f = fopen(path, "r");
    if (f == NULL) {
        printf("  missing %s (run UPDATE_GOLDENS=1 make test)\n", path);
        gCurrentTestFailed = 1;
        return;
    }
    n = fread(stored, 1, sizeof(stored) - 1, f);
    stored[n] = '\0';
    fclose(f);

    if (strcmp(generated, stored) != 0) {
        printf("  board for seed %u does not match %s\n", seed, path);
        printf("  if the change is on purpose: UPDATE_GOLDENS=1 make test\n");
        gCurrentTestFailed = 1;
    }
}

static void test_golden_boards(void) {
    size_t i;
    for (i = 0; i < sizeof(kGoldenSeeds) / sizeof(kGoldenSeeds[0]); i++) {
        check_one_golden(kGoldenSeeds[i]);
    }
}

// ---------------------------------------------------------------------------
// Invariant sweep: things that must hold for every board on any seed.

#define SWEEP_SEEDS 10000

// The dedup pass gives up after 10 tries, so a few boards can keep a
// duplicate. That is existing behavior, not a bug we introduced. The count
// is deterministic for a fixed seed range; if it drifts a little after an
// intentional generation change, re-bless it. If it jumps, look closer.
#define EXPECTED_BOARDS_WITH_DUPLICATES 0

static int board_has_line_duplicates(void) {
    int a, b, i;
    // Same pairs the dedup pass looks at: rows, columns, both diagonals.
    for (i = 0; i < 5; i++) {
        for (a = 0; a < 4; a++) {
            for (b = a + 1; b < 5; b++) {
                if (are_duplicates(&gBingoObjectives[i * 5 + a], &gBingoObjectives[i * 5 + b])
                    || are_duplicates(&gBingoObjectives[a * 5 + i], &gBingoObjectives[b * 5 + i])) {
                    return 1;
                }
            }
        }
    }
    for (a = 0; a < 4; a++) {
        for (b = a + 1; b < 5; b++) {
            if (are_duplicates(&gBingoObjectives[a * 5 + a], &gBingoObjectives[b * 5 + b])
                || are_duplicates(&gBingoObjectives[a * 5 + (4 - a)], &gBingoObjectives[b * 5 + (4 - b)])) {
                return 1;
            }
        }
    }
    return 0;
}

static void test_invariant_sweep(void) {
    u32 seed;
    int i;
    int dupBoards = 0;

    for (seed = 1; seed <= SWEEP_SEEDS; seed++) {
        generate_board(seed);
        for (i = 0; i < 25; i++) {
            struct BingoObjective *o = &gBingoObjectives[i];
            CHECK(o->initialized);
            CHECK(o->type >= BINGO_OBJECTIVE_TYPE_MIN && o->type <= BINGO_OBJECTIVE_TYPE_MAX);
            CHECK(o->state == BINGO_STATE_NONE);
            CHECK(o->title[0] != '\0');
            CHECK(strlen(o->title) < sizeof(o->title));
            if (is_star_type(o->type)) {
                CHECK(o->data.starObjective.course >= 1 && o->data.starObjective.course <= 24);
                CHECK(o->data.starObjective.starIndex >= 0 && o->data.starObjective.starIndex <= 6);
            }
            if (gCurrentTestFailed) {
                printf("  (seed %u, cell %d)\n", seed, i);
                return;
            }
        }
        dupBoards += board_has_line_duplicates();
    }

    if (EXPECTED_BOARDS_WITH_DUPLICATES == -1) {
        printf("  boards with leftover duplicates: %d of %d (bless this number)\n",
               dupBoards, SWEEP_SEEDS);
    } else {
        CHECK_EQ_INT(dupBoards, EXPECTED_BOARDS_WITH_DUPLICATES);
    }
}

// ---------------------------------------------------------------------------
// Weight budget: a limited objective type should never show up on one board
// more often than all its class budgets allow together.
//
// KNOWN BUG: get_random_objective_type can pick an entry whose
// usesRemaining is already 0 (when the random want_sum is 0), and the
// counter then drops to -1, which means "no limit". This test pins down
// exactly how many boards go over budget, out of the 2000 seeds below.
// The bug is still unfixed, but since the splatoon weights joined the
// tables, none of these 2000 seeds happen to trigger it (was 1 before).
#define KNOWN_OVER_BUDGET_BOARDS 0

static void test_weight_budget(void) {
    s32 budget[BINGO_OBJECTIVE_TOTAL_AMOUNT];
    s32 counts[BINGO_OBJECTIVE_TOTAL_AMOUNT];
    u32 seed;
    int overBudgetBoards = 0;
    int i;

    save_or_restore_weights();  // make sure the saved fresh tables exist

    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
        budget[i] = 0;
    }
    for (i = 0; i < sWeightsSizeEasy; i++) {
        budget[sSavedEasy[i].objective] += sSavedEasy[i].usesRemaining == -1 ? 25 : sSavedEasy[i].usesRemaining;
    }
    for (i = 0; i < sWeightsSizeMedium; i++) {
        budget[sSavedMedium[i].objective] += sSavedMedium[i].usesRemaining == -1 ? 25 : sSavedMedium[i].usesRemaining;
    }
    for (i = 0; i < sWeightsSizeHard; i++) {
        budget[sSavedHard[i].objective] += sSavedHard[i].usesRemaining == -1 ? 25 : sSavedHard[i].usesRemaining;
    }
    for (i = 0; i < sWeightsSizeCenter; i++) {
        budget[sSavedCenter[i].objective] += sSavedCenter[i].usesRemaining == -1 ? 25 : sSavedCenter[i].usesRemaining;
    }

    for (seed = 1; seed <= 2000; seed++) {
        generate_board(seed);
        for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
            counts[i] = 0;
        }
        for (i = 0; i < 25; i++) {
            counts[gBingoObjectives[i].type]++;
        }
        for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
            if (counts[i] > budget[i]) {
                overBudgetBoards++;
                break;
            }
        }
    }

    if (KNOWN_OVER_BUDGET_BOARDS == -1) {
        printf("  boards over budget: %d of 2000 (bless this number)\n", overBudgetBoards);
    } else {
        CHECK_EQ_INT(overBudgetBoards, KNOWN_OVER_BUDGET_BOARDS);
    }
}

// ---------------------------------------------------------------------------
// Event simulations: hand-build an objective, feed it game events through
// bingo_update, and watch the state change.

static void reset_sim(void) {
    memset(gBingoObjectives, 0, sizeof(gBingoObjectives));
    gBingoInitialized = 1;
    gbBingosCompleted = 0;
    gbBingoTarget = 1;
    gbBingoTimerDisabled = 0;
    gCurrCourseNum = 0;
    gbStarIndex = 0;
    gbCoinsJustGotten = 0;
    // Give every unused cell a type that ignores most updates, so the cell
    // under test is the only interesting one.
    {
        int i;
        for (i = 0; i < 25; i++) {
            gBingoObjectives[i].type = BINGO_OBJECTIVE_BOWSER;
        }
    }
}

static void test_sim_single_star(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    o->type = BINGO_OBJECTIVE_STAR;
    o->data.starObjective.course = 5;
    o->data.starObjective.starIndex = 3;

    // Wrong course: nothing happens.
    gCurrCourseNum = 4;
    gbStarIndex = 3;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    // Wrong star in the right course: still nothing.
    gCurrCourseNum = 5;
    gbStarIndex = 2;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    // The right star.
    gbStarIndex = 3;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(gbBingosCompleted, 0);
}

static void test_sim_coin_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    o->type = BINGO_OBJECTIVE_COIN;
    o->data.courseCollectableData.course = 2;
    o->data.courseCollectableData.toGet = 50;

    gCurrCourseNum = 2;
    gbCoinsJustGotten = 30;
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 30);

    // Leaving the course throws the progress away.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 0);

    // Coins in another course do not count.
    gCurrCourseNum = 3;
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 0);

    // Enough coins in the right course completes it.
    gCurrCourseNum = 2;
    gbCoinsJustGotten = 50;
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);

    // Completion is sticky: a course change no longer resets it.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
}

static void test_sim_splatoon_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    gGlueHudNumberCalls = 0;
    gGlueHudNumberLast = -1;
    o->type = BINGO_OBJECTIVE_SPLATOON;
    o->data.courseCollectableData.course = 1;
    o->data.courseCollectableData.toGet = 120;

    // Painting in the wrong course does nothing.
    gCurrCourseNum = 2;
    gSplatoonPaintedCount = 40;
    bingo_update(BINGO_UPDATE_SPLATOON_PAINTED);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 0);

    // Painting in the right course tracks the game's counter. 40 is not
    // a milestone, so no HUD toast yet.
    gCurrCourseNum = 1;
    gSplatoonPaintedCount = 40;
    bingo_update(BINGO_UPDATE_SPLATOON_PAINTED);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 40);
    CHECK_EQ_INT(gGlueHudNumberCalls, 0);

    // Every 50th tile posts the icon-x-N HUD toast.
    gSplatoonPaintedCount = 50;
    bingo_update(BINGO_UPDATE_SPLATOON_PAINTED);
    CHECK_EQ_INT(gGlueHudNumberCalls, 1);
    CHECK_EQ_INT(gGlueHudNumberLast, 50);

    // Leaving the course throws the progress away.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 0);

    // Enough paint completes it (no extra toast on the completing tile).
    gSplatoonPaintedCount = 120;
    bingo_update(BINGO_UPDATE_SPLATOON_PAINTED);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(gGlueHudNumberCalls, 1);

    // Completion is sticky.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
}

static void test_sim_kill_collectable(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    int i;
    reset_sim();
    o->type = BINGO_OBJECTIVE_KILL_GOOMBAS;
    o->data.collectableData.toGet = 3;

    for (i = 0; i < 2; i++) {
        bingo_update(BINGO_UPDATE_KILLED_GOOMBA);
    }
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);

    // A different kill type does not count.
    bingo_update(BINGO_UPDATE_KILLED_BOBOMB);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);

    bingo_update(BINGO_UPDATE_KILLED_GOOMBA);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
}

static void test_sim_abz_fail_and_reset(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    o->type = BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE;
    o->data.abcStarObjective.course = 7;
    o->data.abcStarObjective.starIndex = 0;

    // Pressing B in the course fails it for this visit.
    gCurrCourseNum = 7;
    bingo_update(BINGO_UPDATE_B_PRESSED);
    CHECK_EQ_INT(o->state, BINGO_STATE_FAILED_IN_THIS_COURSE);

    // While failed, the star does not complete it.
    gbStarIndex = 0;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_FAILED_IN_THIS_COURSE);

    // B in some other course is fine.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    gCurrCourseNum = 8;
    bingo_update(BINGO_UPDATE_B_PRESSED);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    // Fresh visit, no B press, star gets it.
    gCurrCourseNum = 7;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
}

static void test_sim_timed_star(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    int i;
    reset_sim();
    o->type = BINGO_OBJECTIVE_STAR_TIMED;
    o->data.starTimerObjective.course = 4;
    o->data.starTimerObjective.starIndex = 1;
    o->data.starTimerObjective.maxTime = 10;

    // Ten frames pass: still inside the limit.
    gCurrCourseNum = 4;
    for (i = 0; i < 10; i++) {
        bingo_update(BINGO_UPDATE_TIMER_FRAME_STAR);
    }
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    // Star in time completes it.
    gbStarIndex = 1;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);

    // Fresh objective: run out the clock instead.
    reset_sim();
    o->type = BINGO_OBJECTIVE_STAR_TIMED;
    o->data.starTimerObjective.course = 4;
    o->data.starTimerObjective.starIndex = 1;
    o->data.starTimerObjective.maxTime = 10;
    gCurrCourseNum = 4;
    for (i = 0; i < 11; i++) {
        bingo_update(BINGO_UPDATE_TIMER_FRAME_STAR);
    }
    CHECK_EQ_INT(o->state, BINGO_STATE_FAILED_IN_THIS_COURSE);

    // Too late now.
    gbStarIndex = 1;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_FAILED_IN_THIS_COURSE);

    // Re-entering the course resets the clock and the failure.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.starTimerObjective.timer, 0);
}

static void test_sim_row_completion_wins(void) {
    int i;
    reset_sim();
    // Five star objectives sitting in board column 0, which is a bingo
    // line (the board is stored column-major for win checking).
    for (i = 0; i < 5; i++) {
        struct BingoObjective *o = &gBingoObjectives[i * 5];
        o->type = BINGO_OBJECTIVE_STAR;
        o->data.starObjective.course = 1;
        o->data.starObjective.starIndex = i;
    }

    gCurrCourseNum = 1;
    for (i = 0; i < 5; i++) {
        gbStarIndex = i;
        bingo_update(BINGO_UPDATE_STAR);
    }
    CHECK_EQ_INT(gbBingosCompleted, 1);
}

static void test_win_detection(void) {
    int i;
    generate_board(777);
    CHECK_EQ_INT(bingo_check_win(), 0);

    // Complete row 2 (cells at i + j*5 share row j).
    for (i = 0; i < 5; i++) {
        gBingoObjectives[i * 5 + 2].state = BINGO_STATE_COMPLETE;
    }
    CHECK_EQ_INT(bingo_check_win(), 1);

    // The main diagonal too: two bingos now.
    for (i = 0; i < 5; i++) {
        gBingoObjectives[i * 5 + i].state = BINGO_STATE_COMPLETE;
    }
    CHECK_EQ_INT(bingo_check_win(), 2);
}

int main(void) {
    // BOARD_SEED=n prints that board instead of running tests. Handy for
    // comparing against what the ROM shows on screen, and used as the
    // oracle by web/check.mjs. BOARD_TARGET=n (1, 2, 3, or 12) and
    // BOARD_DISABLE=t1,t2,... (objective type numbers) set the same options
    // the file select screen offers.
    const char *seedArg = getenv("BOARD_SEED");
    if (seedArg != NULL) {
        const char *targetArg = getenv("BOARD_TARGET");
        const char *disableArg = getenv("BOARD_DISABLE");
        if (disableArg != NULL) {
            char *end;
            while (*disableArg != '\0') {
                long type = strtol(disableArg, &end, 10);
                if (end == disableArg) {
                    break;
                }
                if (type >= 0 && type < BINGO_OBJECTIVE_TOTAL_AMOUNT) {
                    gBingoObjectivesDisabled[type] = 1;
                }
                disableArg = (*end == ',') ? end + 1 : end;
            }
        }
        generate_board((u32) strtoul(seedArg, NULL, 10));
        if (targetArg != NULL) {
            // generate_board pins the target to 1; redo setup with the
            // requested target, exactly like a fresh boot with that option.
            save_or_restore_weights();
            memset(gBingoObjectives, 0, sizeof(gBingoObjectives));
            gbBingoTarget = (s32) strtol(targetArg, NULL, 10);
            gbBingosCompleted = 0;
            setup_bingo_objectives((u32) strtoul(seedArg, NULL, 10));
        }
        dump_board(stdout);
        return 0;
    }

    RUN_TEST(test_mt19937_reference);
    RUN_TEST(test_same_seed_same_board);
    RUN_TEST(test_different_seed_different_board);
    RUN_TEST(test_golden_boards);
    RUN_TEST(test_invariant_sweep);
    RUN_TEST(test_weight_budget);
    RUN_TEST(test_sim_single_star);
    RUN_TEST(test_sim_coin_objective);
    RUN_TEST(test_sim_splatoon_objective);
    RUN_TEST(test_sim_kill_collectable);
    RUN_TEST(test_sim_abz_fail_and_reset);
    RUN_TEST(test_sim_timed_star);
    RUN_TEST(test_sim_row_completion_wins);
    RUN_TEST(test_win_detection);
    return test_summary();
}
