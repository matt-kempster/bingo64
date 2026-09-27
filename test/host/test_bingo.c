#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "area.h"
#include "bingo.h"
#include "sm64.h"
#include "engine/rand.h"
#include "harness.h"
#include "splatoon.h"
#include "bingo_tracking_collectables.h"
#include "bingo_tracking_star.h"
#include "level_update.h"

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
struct ObjectiveWeight *get_random_objective_type(enum BingoObjectiveClass class);

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

// BOARD_TARGET still speaks the old numbers (1, 2, 3, or 12 for blackout);
// the game replaced gbBingoTarget with the BingoGameMode enum. Board bytes
// are mode-independent either way.
static enum BingoGameMode mode_from_target(s32 target) {
    switch (target) {
        case 2:  return BINGO_MODE_LINE_2;
        case 3:  return BINGO_MODE_LINE_3;
        case 12: return BINGO_MODE_BLACKOUT;
        default: return BINGO_MODE_LINE_1;
    }
}

static void generate_board(u32 seed) {
    save_or_restore_weights();
    memset(gBingoObjectives, 0, sizeof(gBingoObjectives));
    gbBingoMode = BINGO_MODE_LINE_1;
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
        // B and Z init only starObjective: abcStarObjective.hint is stale
        // union bytes for them (BOARD_SEED=1302 used to segfault here).
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_COINLESS:
            fprintf(out, " course=%d star=%d",
                    o->data.starObjective.course, o->data.starObjective.starIndex);
            break;
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
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
        case BINGO_OBJECTIVE_RANDOM_STARS:
        case BINGO_OBJECTIVE_COIN:
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
        case BINGO_OBJECTIVE_RANDOM_RED_COINS:
        case BINGO_OBJECTIVE_SPLATOON:
            fprintf(out, " course=%d toGet=%d",
                    o->data.courseCollectableData.course, o->data.courseCollectableData.toGet);
            break;
        case BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS:
        case BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS:
        case BINGO_OBJECTIVE_COINS_MULTIPLE_LEVELS:
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
// Weighting expect test: an aggregate fingerprint of the board generator.
//
// Generates WEIGHTING_BOARDS boards and tabulates (a) how often each
// objective type appears, split by class, with the range of its target
// numbers, and (b) how often each course is the pinned target of a cell.
// The table is compared against golden/weighting.txt like the golden
// boards: any change to the weight tables, class grid, target ranges, or
// course selection shows up as a reviewable diff. Bless on purpose with
// UPDATE_GOLDENS=1 make test.
//
// Names are spelled out so diffs stay readable when the enum shifts; a
// type without a name prints as type_NN — add the name when adding the
// objective.

#define WEIGHTING_BOARDS 2000

extern char *courseAbbreviations[24];

static const char *kTypeNames[BINGO_OBJECTIVE_TOTAL_AMOUNT] = {
    [BINGO_OBJECTIVE_STAR] = "STAR",
    [BINGO_OBJECTIVE_STAR_TIMED] = "STAR_TIMED",
    [BINGO_OBJECTIVE_STAR_TTC_RANDOM] = "STAR_TTC_RANDOM",
    [BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE] = "STAR_A_BUTTON_CHALLENGE",
    [BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE] = "STAR_B_BUTTON_CHALLENGE",
    [BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE] = "STAR_Z_BUTTON_CHALLENGE",
    [BINGO_OBJECTIVE_STAR_CLICK_GAME] = "STAR_CLICK_GAME",
    [BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK] = "STAR_REVERSE_JOYSTICK",
    [BINGO_OBJECTIVE_STAR_GREEN_DEMON] = "STAR_GREEN_DEMON",
    [BINGO_OBJECTIVE_STAR_DAREDEVIL] = "STAR_DAREDEVIL",
    [BINGO_OBJECTIVE_COIN] = "COIN",
    [BINGO_OBJECTIVE_1UPS_IN_LEVEL] = "1UPS_IN_LEVEL",
    [BINGO_OBJECTIVE_STARS_IN_LEVEL] = "STARS_IN_LEVEL",
    [BINGO_OBJECTIVE_RANDOM_RED_COINS] = "RANDOM_RED_COINS",
    [BINGO_OBJECTIVE_SPLATOON] = "SPLATOON",
    [BINGO_OBJECTIVE_RANDOM_STARS] = "RANDOM_STARS",
    [BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS] = "DANGEROUS_WALL_KICKS",
    [BINGO_OBJECTIVE_BOWSER] = "BOWSER",
    [BINGO_OBJECTIVE_ROOF_WITHOUT_CANNON] = "ROOF_WITHOUT_CANNON",
    [BINGO_OBJECTIVE_RACING_STARS] = "RACING_STARS",
    [BINGO_OBJECTIVE_SECRETS_STARS] = "SECRETS_STARS",
    [BINGO_OBJECTIVE_LIVES] = "LIVES",
    [BINGO_OBJECTIVE_CANNON_STARS] = "CANNON_STARS",
    [BINGO_OBJECTIVE_MULTICOIN] = "MULTICOIN",
    [BINGO_OBJECTIVE_MULTISTAR] = "MULTISTAR",
    [BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS] = "STARS_MULTIPLE_LEVELS",
    [BINGO_OBJECTIVE_BLJ] = "BLJ",
    [BINGO_OBJECTIVE_LOSE_MARIO_HAT] = "LOSE_MARIO_HAT",
    [BINGO_OBJECTIVE_SIGNPOST] = "SIGNPOST",
    [BINGO_OBJECTIVE_POLES] = "POLES",
    [BINGO_OBJECTIVE_SHOOT_CANNONS] = "SHOOT_CANNONS",
    [BINGO_OBJECTIVE_RED_COIN] = "RED_COIN",
    [BINGO_OBJECTIVE_EXCLAMATION_MARK_BOX] = "EXCLAMATION_MARK_BOX",
    [BINGO_OBJECTIVE_WING_CAP_BOX] = "WING_CAP_BOX",
    [BINGO_OBJECTIVE_VANISH_CAP_BOX] = "VANISH_CAP_BOX",
    [BINGO_OBJECTIVE_METAL_CAP_BOX] = "METAL_CAP_BOX",
    [BINGO_OBJECTIVE_AMPS] = "AMPS",
    [BINGO_OBJECTIVE_KILL_GOOMBAS] = "KILL_GOOMBAS",
    [BINGO_OBJECTIVE_KILL_BOBOMBS] = "KILL_BOBOMBS",
    [BINGO_OBJECTIVE_KILL_SPINDRIFTS] = "KILL_SPINDRIFTS",
    [BINGO_OBJECTIVE_KILL_MR_IS] = "KILL_MR_IS",
    [BINGO_OBJECTIVE_KILL_SCUTTLEBUGS] = "KILL_SCUTTLEBUGS",
    [BINGO_OBJECTIVE_KILL_BULLIES] = "KILL_BULLIES",
    [BINGO_OBJECTIVE_KILL_CHUCKYAS] = "KILL_CHUCKYAS",
    [BINGO_OBJECTIVE_KILL_WHOMPS] = "KILL_WHOMPS",
    [BINGO_OBJECTIVE_KILL_BOOS] = "KILL_BOOS",
    [BINGO_OBJECTIVE_KILL_SNUFITS] = "KILL_SNUFITS",
    [BINGO_OBJECTIVE_HURT_BY_CLAMS] = "HURT_BY_CLAMS",
    [BINGO_OBJECTIVE_KILL_FLY_GUYS] = "KILL_FLY_GUYS",
    [BINGO_OBJECTIVE_KILL_MR_BLIZZARDS] = "KILL_MR_BLIZZARDS",
    [BINGO_OBJECTIVE_KILL_SKEETERS] = "KILL_SKEETERS",
    [BINGO_OBJECTIVE_KILL_KOOPAS] = "KILL_KOOPAS",
    [BINGO_OBJECTIVE_CRUSHED] = "CRUSHED",
    [BINGO_OBJECTIVE_UNIQUE_DEATHS] = "UNIQUE_DEATHS",
    [BINGO_OBJECTIVE_BLUE_COIN] = "BLUE_COIN",
    [BINGO_OBJECTIVE_RED_COIN_STARS] = "RED_COIN_STARS",
    [BINGO_OBJECTIVE_STAR_COINLESS] = "STAR_COINLESS",
    [BINGO_OBJECTIVE_WARP_PADS] = "WARP_PADS",
    [BINGO_OBJECTIVE_KOOPA_SHELLS] = "KOOPA_SHELLS",
    [BINGO_OBJECTIVE_SPIN_HEARTS] = "SPIN_HEARTS",
    [BINGO_OBJECTIVE_OPEN_CANNONS] = "OPEN_CANNONS",
    [BINGO_OBJECTIVE_TOAD_STARS] = "TOAD_STARS",
    [BINGO_OBJECTIVE_MIPS] = "MIPS",
    [BINGO_OBJECTIVE_HUNDRED_COIN_STARS] = "HUNDRED_COIN_STARS",
    [BINGO_OBJECTIVE_CASTLE_SECRET_STARS] = "CASTLE_SECRET_STARS",
    [BINGO_OBJECTIVE_CAPS_WORN] = "CAPS_WORN",
    [BINGO_OBJECTIVE_PURPLE_SWITCHES] = "PURPLE_SWITCHES",
    [BINGO_OBJECTIVE_STUCK_IN_GROUND] = "STUCK_IN_GROUND",
    [BINGO_OBJECTIVE_COINS_MULTIPLE_LEVELS] = "COINS_MULTIPLE_LEVELS",
    [BINGO_OBJECTIVE_1UPS_MULTIPLE_LEVELS] = "1UPS_MULTIPLE_LEVELS",
};

// The course a cell is pinned to, or 0 if the objective is not
// course-pinned (global counters, multi-course goals, Bowser levels).
static s32 cell_pinned_course(struct BingoObjective *o) {
    switch (o->type) {
        case BINGO_OBJECTIVE_STAR:
        case BINGO_OBJECTIVE_STAR_TTC_RANDOM:
        case BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK:
        case BINGO_OBJECTIVE_STAR_GREEN_DEMON:
        case BINGO_OBJECTIVE_STAR_DAREDEVIL:
            return o->data.starObjective.course;
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_COINLESS:
            return o->data.abcStarObjective.course;
        case BINGO_OBJECTIVE_STAR_TIMED:
            return o->data.starTimerObjective.course;
        case BINGO_OBJECTIVE_STAR_CLICK_GAME:
            return o->data.starClicksObjective.course;
        case BINGO_OBJECTIVE_COIN:
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
        case BINGO_OBJECTIVE_RANDOM_RED_COINS:
        case BINGO_OBJECTIVE_SPLATOON:
            return o->data.courseCollectableData.course;
        default:
            return 0;
    }
}

// The headline target number of a cell, for range tracking; -1 if the
// objective has no meaningful count (plain stars, Bowser, roof).
static s32 cell_target(struct BingoObjective *o) {
    if (is_star_type(o->type)) {
        return -1;
    }
    switch (o->type) {
        case BINGO_OBJECTIVE_COIN:
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
        case BINGO_OBJECTIVE_RANDOM_RED_COINS:
        case BINGO_OBJECTIVE_SPLATOON:
            return o->data.courseCollectableData.toGet;
        case BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS:
        case BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS:
        case BINGO_OBJECTIVE_COINS_MULTIPLE_LEVELS:
            return o->data.multiCourseCollectableData.toGetTotal;
        case BINGO_OBJECTIVE_BOWSER:
        case BINGO_OBJECTIVE_ROOF_WITHOUT_CANNON:
            return -1;
        default:
            return o->data.collectableData.toGet;
    }
}

static void check_golden_text(const char *path, const char *generated) {
    char stored[32768];
    size_t n;
    FILE *f;

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
        printf("  weighting fingerprint does not match %s\n", path);
        printf("  if the change is on purpose: UPDATE_GOLDENS=1 make test\n");
        gCurrentTestFailed = 1;
    }
}

static void test_weighting_expect(void) {
    static s32 typeTotal[BINGO_OBJECTIVE_TOTAL_AMOUNT];
    static s32 typeByClass[BINGO_OBJECTIVE_TOTAL_AMOUNT][4];
    static s32 targetLo[BINGO_OBJECTIVE_TOTAL_AMOUNT];
    static s32 targetHi[BINGO_OBJECTIVE_TOTAL_AMOUNT];
    static s32 courseCounts[25];  // 1..24
    static char generated[32768];
    s32 cells = WEIGHTING_BOARDS * 25;
    u32 seed;
    int i, t;
    size_t off = 0;

    memset(typeTotal, 0, sizeof(typeTotal));
    memset(typeByClass, 0, sizeof(typeByClass));
    memset(courseCounts, 0, sizeof(courseCounts));
    for (t = 0; t < BINGO_OBJECTIVE_TOTAL_AMOUNT; t++) {
        targetLo[t] = -1;
        targetHi[t] = -1;
    }

    for (seed = 1; seed <= WEIGHTING_BOARDS; seed++) {
        generate_board(seed);
        for (i = 0; i < 25; i++) {
            struct BingoObjective *o = &gBingoObjectives[i];
            s32 course = cell_pinned_course(o);
            s32 target = cell_target(o);
            int cls = (int) o->class;
            typeTotal[o->type]++;
            if (cls >= 0 && cls < 4) {
                typeByClass[o->type][cls]++;
            }
            if (course >= 1 && course <= 24) {
                courseCounts[course]++;
            }
            if (target >= 0) {
                if (targetLo[o->type] == -1 || target < targetLo[o->type]) {
                    targetLo[o->type] = target;
                }
                if (target > targetHi[o->type]) {
                    targetHi[o->type] = target;
                }
            }
        }
    }

#define EMIT(...) off += snprintf(generated + off, sizeof(generated) - off, __VA_ARGS__)

    EMIT("Board generator fingerprint over %d boards (%d cells).\n",
         WEIGHTING_BOARDS, cells);
    EMIT("permille = cells per 1000 across all boards.\n");
    EMIT("\n[objective types]  count  permille  easy  med  hard  cent  target\n");
    for (t = 0; t < BINGO_OBJECTIVE_TOTAL_AMOUNT; t++) {
        char name[40];
        if (typeTotal[t] == 0) {
            continue;
        }
        if (kTypeNames[t] != NULL) {
            snprintf(name, sizeof(name), "%s", kTypeNames[t]);
        } else {
            snprintf(name, sizeof(name), "type_%02d", t);
        }
        EMIT("%-24s %6d %8d %5d %4d %5d %5d",
             name, typeTotal[t], (typeTotal[t] * 1000) / cells,
             typeByClass[t][BINGO_CLASS_EASY], typeByClass[t][BINGO_CLASS_MEDIUM],
             typeByClass[t][BINGO_CLASS_HARD], typeByClass[t][BINGO_CLASS_CENTER]);
        if (targetLo[t] >= 0) {
            EMIT("  %d..%d", targetLo[t], targetHi[t]);
        } else {
            EMIT("  -");
        }
        EMIT("\n");
    }

    EMIT("\n[course pins]  count  permille-of-pinned\n");
    {
        s32 pinned = 0;
        for (i = 1; i <= 24; i++) {
            pinned += courseCounts[i];
        }
        for (i = 1; i <= 24; i++) {
            EMIT("%-6s %6d %6d\n", courseAbbreviations[i - 1], courseCounts[i],
                 pinned > 0 ? (courseCounts[i] * 1000) / pinned : 0);
        }
        EMIT("unpinned cells: %d of %d\n", cells - pinned, cells);
    }
#undef EMIT

    check_golden_text("golden/weighting.txt", generated);
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
// Regression: get_random_objective_type used to stop its running-total
// scan at `sum >= want_sum`, so a want_sum of 0 returned row 0 even when
// its usesRemaining was already 0; the caller then decremented it to -1,
// which means NO_LIMIT, and the exhausted type became unlimited (seed 543
// dealt 5 timed stars against a budget of 4 when this was found).
static int board_over_budget(const s32 *budget) {
    s32 counts[BINGO_OBJECTIVE_TOTAL_AMOUNT];
    int i;

    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
        counts[i] = 0;
    }
    for (i = 0; i < 25; i++) {
        counts[gBingoObjectives[i].type]++;
    }
    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
        if (counts[i] > budget[i]) {
            printf("  type %d dealt %d times, budget %d\n", i, counts[i], budget[i]);
            return 1;
        }
    }
    return 0;
}

static void test_weight_budget(void) {
    s32 budget[BINGO_OBJECTIVE_TOTAL_AMOUNT];
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

    // The original repro seed, then a broad sweep.
    generate_board(543);
    CHECK(!board_over_budget(budget));
    for (seed = 1; seed <= 2000; seed++) {
        generate_board(seed);
        if (board_over_budget(budget)) {
            printf("  (seed %u)\n", seed);
            overBudgetBoards++;
        }
    }
    CHECK_EQ_INT(overBudgetBoards, 0);
}

// The draw itself: with row 0 of every class table exhausted, thousands of
// draws (enough to hit want_sum == 0 many times over) must never return an
// exhausted or zero-weight row.
static void test_weighted_pick_skips_exhausted(void) {
    struct ObjectiveWeight *tables[4] = { sWeightsEasy, sWeightsMedium, sWeightsHard, sWeightsCenter };
    s32 sizes[4] = { sWeightsSizeEasy, sWeightsSizeMedium, sWeightsSizeHard, sWeightsSizeCenter };
    enum BingoObjectiveClass classes[4] = { BINGO_CLASS_EASY, BINGO_CLASS_MEDIUM,
                                            BINGO_CLASS_HARD, BINGO_CLASS_CENTER };
    struct ObjectiveWeight *pick;
    int t, n, bad = 0, row0 = 0;

    generate_board(1);  // sane options/mask state
    for (t = 0; t < 4; t++) {
        save_or_restore_weights();
        if (sizes[t] < 2) {
            continue;
        }
        tables[t][0].usesRemaining = 0;
        init_genrand(4242 + t);
        for (n = 0; n < 20000; n++) {
            pick = get_random_objective_type(classes[t]);
            if (pick == NULL) {
                continue;
            }
            if (pick == &tables[t][0]) {
                row0++;
            }
            if (pick->usesRemaining == 0 || pick->weight == 0) {
                bad++;
            }
        }
    }
    save_or_restore_weights();
    CHECK_EQ_INT(row0, 0);
    CHECK_EQ_INT(bad, 0);
}

static void test_repeated_generation_resets_budgets(void) {
    struct BingoObjective first[25];
    int round;
    // Regression: usesRemaining budgets used to deplete across in-process
    // generations (netplay rematches) until a class's weight sum hit 0 and
    // get_random_objective_type faulted on `% sum` by about the 4th board.
    // Deliberately NOT calling save_or_restore_weights here: the game must
    // reset its own budgets now, and the reset must return the tables to
    // pristine (same seed, same board, no matter how many boards came
    // before).
    for (round = 0; round < 12; round++) {
        memset(gBingoObjectives, 0, sizeof(gBingoObjectives));
        gbBingoMode = BINGO_MODE_LINE_1;
        gbBingosCompleted = 0;
        setup_bingo_objectives(4242);
        if (round == 0) {
            memcpy(first, gBingoObjectives, sizeof(first));
        }
    }
    CHECK(memcmp(first, gBingoObjectives, sizeof(first)) == 0);
}

// ---------------------------------------------------------------------------
// Event simulations: hand-build an objective, feed it game events through
// bingo_update, and watch the state change.

static void reset_sim(void) {
    memset(gBingoObjectives, 0, sizeof(gBingoObjectives));
    gBingoInitialized = 1;
    gbBingosCompleted = 0;
    gbBingoMode = BINGO_MODE_LINE_1;
    gbBingoTimerDisabled = 0;
    gCurrCourseNum = 0;
    gbStarIndex = 0;
    gbCoinsJustGotten = 0;
    gbStarFromCannon = 0;
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

static void test_sim_cannon_stars_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    gGlueHudNumberCalls = 0;
    gGlueHudNumberLast = -1;
    o->type = BINGO_OBJECTIVE_CANNON_STARS;
    o->data.collectableData.toGet = 2;

    // An ordinary star grab (not from a cannon) does nothing.
    gCurrCourseNum = 1;
    gbStarIndex = 3;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 0);

    // A star hit mid-cannon-shot counts, in any course, and updates the HUD.
    gbStarFromCannon = 1;
    bingo_update(BINGO_UPDATE_STAR);
    gbStarFromCannon = 0;
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    CHECK_EQ_INT(gGlueHudNumberCalls, 1);
    CHECK_EQ_INT(gGlueHudNumberLast, 1);

    // Leaving the course keeps the progress (it is a cross-course total).
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    // Other events with the flag stale-set must not count.
    gbStarFromCannon = 1;
    bingo_update(BINGO_UPDATE_COIN);
    gbStarFromCannon = 0;
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    // The second cannon star completes it.
    gCurrCourseNum = 6;
    gbStarIndex = 5;
    gbStarFromCannon = 1;
    bingo_update(BINGO_UPDATE_STAR);
    gbStarFromCannon = 0;
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);
}

static void test_sim_red_coin_stars_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_star_reset();
    gGlueHudNumberCalls = 0;
    gGlueHudNumberLast = -1;
    o->type = BINGO_OBJECTIVE_RED_COIN_STARS;
    o->data.collectableData.toGet = 2;

    // A star that isn't a red coin star (BOB act 1) does nothing.
    // (bingo_set_star indexes courses from 0, stars by STAR_INDEX_ACT_n.)
    gCurrCourseNum = COURSE_BOB;
    gbStarIndex = 0;
    bingo_set_star(COURSE_BOB - 1, 0);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 0);

    // BOB's red coin star (act 4) counts and updates the HUD.
    gbStarIndex = 3;
    bingo_set_star(COURSE_BOB - 1, 3);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    CHECK_EQ_INT(gGlueHudNumberCalls, 1);
    CHECK_EQ_INT(gGlueHudNumberLast, 1);

    // Re-collecting the same red coin star doesn't count twice.
    bingo_set_star(COURSE_BOB - 1, 3);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    CHECK_EQ_INT(gGlueHudNumberCalls, 1);

    // Leaving the course keeps the progress; other events don't count.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    bingo_update(BINGO_UPDATE_RED_COIN);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    // BITDW's red coin star (a Bowser course, act 1) completes it.
    gCurrCourseNum = COURSE_BITDW;
    gbStarIndex = 0;
    bingo_set_star(COURSE_BITDW - 1, 0);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);
}

static void test_sim_hundred_coin_stars_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_star_reset();
    gGlueHudNumberCalls = 0;
    gGlueHudNumberLast = -1;
    o->type = BINGO_OBJECTIVE_HUNDRED_COIN_STARS;
    o->data.collectableData.toGet = 2;

    // Another WF star (act 1) does nothing.
    gCurrCourseNum = COURSE_WF;
    gbStarIndex = 0;
    bingo_set_star(COURSE_WF - 1, 0);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 0);

    // WF's 100-coin star counts and updates the HUD.
    gbStarIndex = 6;
    bingo_set_star(COURSE_WF - 1, 6);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    CHECK_EQ_INT(gGlueHudNumberCalls, 1);
    CHECK_EQ_INT(gGlueHudNumberLast, 1);

    // Re-collecting it doesn't count twice.
    bingo_set_star(COURSE_WF - 1, 6);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    CHECK_EQ_INT(gGlueHudNumberCalls, 1);

    // Bit 6 in a secret course isn't a 100-coin star.
    gCurrCourseNum = COURSE_BITDW;
    bingo_set_star(COURSE_BITDW - 1, 6);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    // RR's (the last main course) completes it.
    gCurrCourseNum = COURSE_RR;
    bingo_set_star(COURSE_RR - 1, 6);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);
}

static void test_sim_castle_secret_stars_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_star_reset();
    gGlueHudNumberCalls = 0;
    gGlueHudNumberLast = -1;
    o->type = BINGO_OBJECTIVE_CASTLE_SECRET_STARS;
    o->data.collectableData.toGet = 4;

    // A main-course star does nothing.
    gCurrCourseNum = COURSE_BOB;
    gbStarIndex = 0;
    bingo_set_star(COURSE_BOB - 1, 0);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->data.collectableData.gotten, 0);

    // Both slide stars count.
    gCurrCourseNum = COURSE_PSS;
    bingo_set_star(COURSE_PSS - 1, 0);
    bingo_update(BINGO_UPDATE_STAR);
    bingo_set_star(COURSE_PSS - 1, 1);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);
    CHECK_EQ_INT(gGlueHudNumberLast, 2);

    // Re-collecting one doesn't count twice.
    bingo_set_star(COURSE_PSS - 1, 0);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);

    // A Toad star (castle "course", bingo_set_star(-1, i)) counts.
    gCurrCourseNum = COURSE_NONE;
    bingo_set_star(-1, 0);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 3);

    // A MIPS star (castle bit 3) completes it.
    bingo_set_star(-1, 3);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 4);
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

static void test_sim_unique_deaths(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    gGlueHudNumberCalls = 0;
    gGlueHudNumberLast = -1;
    o->type = BINGO_OBJECTIVE_UNIQUE_DEATHS;
    o->data.collectableFlagsData.toGet = 3;

    // Each new way of dying counts once and posts a HUD toast.
    bingo_track_death(ACT_QUICKSAND_DEATH);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(gGlueHudNumberCalls, 1);
    CHECK_EQ_INT(gGlueHudNumberLast, 1);

    // Dying the same way again does nothing.
    bingo_track_death(ACT_QUICKSAND_DEATH);
    CHECK_EQ_INT(gGlueHudNumberCalls, 1);

    bingo_track_death(ACT_DROWNING);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(gGlueHudNumberLast, 2);

    // A non-death action maps to falling out of the level.
    bingo_track_death(0);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);

    // Completion is sticky and doesn't re-toast.
    bingo_track_death(ACT_LAVA_BOOST);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(gGlueHudNumberCalls, 2);
}

static void test_sim_random_stars_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    o->type = BINGO_OBJECTIVE_RANDOM_STARS;
    o->data.courseCollectableData.course = 7;
    o->data.courseCollectableData.toGet = 3;

    // Without the modifier active, collections do nothing.
    gCurrCourseNum = 7;
    bingo_set_rando_star(7, 0);
    bingo_update(BINGO_UPDATE_GOT_RANDOM_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 0);

    // With the modifier active, the count tracks the per-course flags.
    gBingoRandomStarsActive = 1;
    bingo_update(BINGO_UPDATE_GOT_RANDOM_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 1);

    // Stars in another course do not help this objective.
    bingo_set_rando_star(8, 1);
    bingo_update(BINGO_UPDATE_GOT_RANDOM_STAR);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 1);

    // Collecting the same star again is idempotent (bitflag semantics).
    bingo_set_rando_star(7, 0);
    bingo_update(BINGO_UPDATE_GOT_RANDOM_STAR);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 1);

    // All three stars complete the objective.
    bingo_set_rando_star(7, 1);
    bingo_set_rando_star(7, 2);
    bingo_update(BINGO_UPDATE_GOT_RANDOM_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);

    // Completion is sticky across course changes.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    gBingoRandomStarsActive = 0;
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

static void test_sim_blue_coin_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    u32 uidA, uidB;
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_BLUE_COIN;
    o->data.collectableData.toGet = 3;

    // Yellow coins (and a blue coin's own BINGO_UPDATE_COIN, worth 5)
    // feed the coin counters, not this objective.
    gCurrCourseNum = 1;
    gbCoinsJustGotten = 1;
    bingo_update(BINGO_UPDATE_COIN);
    gbCoinsJustGotten = 5;
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(o->data.collectableData.gotten, 0);

    // Red coins are a different collectable.
    bingo_update(BINGO_UPDATE_RED_COIN);
    CHECK_EQ_INT(o->data.collectableData.gotten, 0);

    bingo_update(BINGO_UPDATE_BLUE_COIN);
    bingo_update(BINGO_UPDATE_BLUE_COIN);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);

    // It is a cross-course total: leaving the course keeps the progress.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);

    gCurrCourseNum = 7;
    bingo_update(BINGO_UPDATE_BLUE_COIN);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);

    // interact_coin only fires the event for a source's first coin: the
    // blue coin UID range exists and dedupes by course + position.
    gCurrCourseNum = 7;
    uidA = get_unique_id(BINGO_UPDATE_BLUE_COIN, 100.0f, 200.0f, 300.0f);
    uidB = get_unique_id(BINGO_UPDATE_BLUE_COIN, 100.0f, 200.0f, 301.0f);
    CHECK(uidA != (u32) -1);
    CHECK(uidA != uidB);
    CHECK_EQ_INT(get_unique_id(BINGO_UPDATE_BLUE_COIN, 100.0f, 200.0f, 300.0f), uidA);
    CHECK_EQ_INT(is_new_kill(BINGO_UPDATE_BLUE_COIN, uidA), 1);
    CHECK_EQ_INT(is_new_kill(BINGO_UPDATE_BLUE_COIN, uidA), 0);
    CHECK_EQ_INT(is_new_kill(BINGO_UPDATE_BLUE_COIN, uidB), 1);
    // The same spot in another course is a different coin.
    gCurrCourseNum = 8;
    CHECK(get_unique_id(BINGO_UPDATE_BLUE_COIN, 100.0f, 200.0f, 300.0f) != uidA);
    bingo_tracking_collectables_reset();
}

// Warp pads: the pair counts once, whichever pad you leave from, and the
// same node ids in another area or course are a different pair.
static void test_sim_warp_pads_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_WARP_PADS;
    o->data.collectableData.toGet = 3;

    gCurrCourseNum = COURSE_BOB;
    CHECK_EQ_INT(bingo_track_warp_pad(1, 0x0B, 0x0C), 1);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    // Back through the partner pad, and again the first way: same pair.
    CHECK_EQ_INT(bingo_track_warp_pad(1, 0x0C, 0x0B), 0);
    CHECK_EQ_INT(bingo_track_warp_pad(1, 0x0B, 0x0C), 0);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    // Leaving and re-entering the course doesn't reset it.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(bingo_track_warp_pad(1, 0x0C, 0x0B), 0);

    // BOB's other pair.
    CHECK_EQ_INT(bingo_track_warp_pad(1, 0x0D, 0x0E), 1);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    // Same node ids in a different area / course are different pads.
    gCurrCourseNum = COURSE_SSL;
    CHECK_EQ_INT(bingo_track_warp_pad(2, 0x0B, 0x0C), 1);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    bingo_tracking_collectables_reset();
}

// Koopa shells: a shell counts once per source; the respawned shell from
// the same ! box carries the same UID and doesn't count again.
static void test_sim_koopa_shells_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    u32 boxShell, boxShellAgain, waterShell;
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_KOOPA_SHELLS;
    o->data.collectableData.toGet = 2;

    // interact_koopa_shell: fire only for a new UID.
    gCurrCourseNum = COURSE_LLL;
    boxShell = get_unique_id(BINGO_UPDATE_KOOPA_SHELL, -1000.0f, 300.0f, 2000.0f);
    CHECK(boxShell != (u32) -1);
    if (is_new_kill(BINGO_UPDATE_KOOPA_SHELL, boxShell)) {
        bingo_update(BINGO_UPDATE_KOOPA_SHELL);
    }
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    // The box respawns the shell on a later visit: same key, no count.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    boxShellAgain = get_unique_id(BINGO_UPDATE_KOOPA_SHELL, -1000.0f, 300.0f, 2000.0f);
    CHECK_EQ_INT(boxShellAgain, boxShell);
    CHECK_EQ_INT(is_new_kill(BINGO_UPDATE_KOOPA_SHELL, boxShellAgain), 0);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    // Other collectables' events don't count.
    bingo_update(BINGO_UPDATE_BLUE_COIN);
    bingo_update(BINGO_UPDATE_EXCLAMATION_MARK_BOX);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    // The JRB underwater shell is a second source.
    gCurrCourseNum = COURSE_JRB;
    waterShell = get_unique_id(BINGO_UPDATE_KOOPA_SHELL, -1480.0f, -1000.0f, 4820.0f);
    CHECK(waterShell != boxShell);
    if (is_new_kill(BINGO_UPDATE_KOOPA_SHELL, waterShell)) {
        bingo_update(BINGO_UPDATE_KOOPA_SHELL);
    }
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    bingo_tracking_collectables_reset();
}

// Spinning hearts: first spin of each heart counts, re-spinning doesn't.
static void test_sim_spin_hearts_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_SPIN_HEARTS;
    o->data.collectableData.toGet = 3;

    gCurrCourseNum = COURSE_RR;
    CHECK_EQ_INT(bingo_count_unique_source(BINGO_UPDATE_SPIN_HEART, -550.0f, -1050.0f, -50.0f), 1);
    CHECK_EQ_INT(bingo_count_unique_source(BINGO_UPDATE_SPIN_HEART, -550.0f, -1050.0f, -50.0f), 0);
    CHECK_EQ_INT(bingo_count_unique_source(BINGO_UPDATE_SPIN_HEART, -7071.0f, -1705.0f, -31.0f), 1);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);

    // Coming back later: still the same heart.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(bingo_count_unique_source(BINGO_UPDATE_SPIN_HEART, -550.0f, -1050.0f, -50.0f), 0);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    gCurrCourseNum = COURSE_BOB;
    CHECK_EQ_INT(bingo_count_unique_source(BINGO_UPDATE_SPIN_HEART, 3603.0f, 3659.0f, -7070.0f), 1);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    bingo_tracking_collectables_reset();
}

void describe_objective(struct BingoObjective *objective, char *desc);

static void test_sim_caps_worn_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    char desc[300];
    reset_sim();
    o->type = BINGO_OBJECTIVE_CAPS_WORN;
    o->data.collectableFlagsData.toGet = 3;
    o->data.collectableFlagsData.flags = 0;

    bingo_update(BINGO_UPDATE_WORE_WING_CAP);
    bingo_update(BINGO_UPDATE_WORE_WING_CAP);  // the same cap twice
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    describe_objective(o, desc);
    CHECK(strstr(desc, "Still need: Metal, Vanish") != NULL);

    bingo_update(BINGO_UPDATE_WORE_VANISH_CAP);
    describe_objective(o, desc);
    CHECK(strstr(desc, "Still need: Metal") != NULL);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    bingo_update(BINGO_UPDATE_WORE_METAL_CAP);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
}

static void test_sim_purple_switches_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_PURPLE_SWITCHES;
    o->data.collectableData.toGet = 2;
    o->data.collectableData.gotten = 0;

    gCurrCourseNum = COURSE_WDW;
    CHECK_EQ_INT(bingo_count_unique_source(BINGO_UPDATE_PURPLE_SWITCH, 100.0f, 200.0f, 300.0f), 1);
    // Re-pressing the same switch (it pops back up) counts once.
    CHECK_EQ_INT(bingo_count_unique_source(BINGO_UPDATE_PURPLE_SWITCH, 100.0f, 200.0f, 300.0f), 0);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    gCurrCourseNum = COURSE_DDD;
    CHECK_EQ_INT(bingo_count_unique_source(BINGO_UPDATE_PURPLE_SWITCH, 100.0f, 200.0f, 300.0f), 1);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    bingo_tracking_collectables_reset();
}

// Stuck in the ground: once per course. The tile sits in cell 0 on
// purpose: per-course credit used to key on the bare cell index, and cell
// 0 aliased the UID table's empty-slot marker, so alternating two courses
// re-counted forever.
static void test_sim_stuck_in_ground_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_STUCK_IN_GROUND;
    o->data.collectableData.toGet = 3;
    o->data.collectableData.gotten = 0;

    gCurrCourseNum = COURSE_SL;
    bingo_update(BINGO_UPDATE_STUCK_IN_GROUND);
    bingo_update(BINGO_UPDATE_STUCK_IN_GROUND);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    gCurrCourseNum = COURSE_CCM;
    bingo_update(BINGO_UPDATE_STUCK_IN_GROUND);
    gCurrCourseNum = COURSE_SL;
    bingo_update(BINGO_UPDATE_STUCK_IN_GROUND);
    gCurrCourseNum = COURSE_CCM;
    bingo_update(BINGO_UPDATE_STUCK_IN_GROUND);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    gCurrCourseNum = COURSE_WMOTR;
    bingo_update(BINGO_UPDATE_STUCK_IN_GROUND);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    bingo_tracking_collectables_reset();
}

// The same cell-0 regression for BLJ, which shares the per-course keying.
static void test_sim_blj_cell0_alternating_courses(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_BLJ;
    o->data.collectableData.toGet = 5;
    o->data.collectableData.gotten = 0;

    gCurrCourseNum = COURSE_BOB;
    bingo_update(BINGO_UPDATE_BLJ);
    gCurrCourseNum = COURSE_WF;
    bingo_update(BINGO_UPDATE_BLJ);
    gCurrCourseNum = COURSE_BOB;
    bingo_update(BINGO_UPDATE_BLJ);
    gCurrCourseNum = COURSE_WF;
    bingo_update(BINGO_UPDATE_BLJ);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);
    bingo_tracking_collectables_reset();
}

// Chuckya kills key on the spawn point (oHome, from the level macro). They
// used to key on (0, 0, 0), the UID table's empty-slot marker, so each
// course's Chuckya re-claimed the one slot and reset its kill: alternating
// WDW and TTM re-counted the same two Chuckyas forever.
static void test_sim_chuckya_alternating_courses(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    u32 wdw, ttm;
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_KILL_CHUCKYAS;
    o->data.collectableData.toGet = 3;
    o->data.collectableData.gotten = 0;

#define KILL_CHUCKYA(uid)                                                 \
    do {                                                                  \
        if (is_new_kill(BINGO_UPDATE_KILLED_CHUCKYA, (uid))) {            \
            bingo_update(BINGO_UPDATE_KILLED_CHUCKYA);                    \
        }                                                                 \
    } while (0)

    gCurrCourseNum = COURSE_WDW;
    wdw = get_unique_id(BINGO_UPDATE_KILLED_CHUCKYA, -2963.0f, 3840.0f, -3063.0f);
    CHECK(wdw != (u32) -1);
    KILL_CHUCKYA(wdw);
    gCurrCourseNum = COURSE_TTM;
    ttm = get_unique_id(BINGO_UPDATE_KILLED_CHUCKYA, -2676.0f, -2145.0f, 2923.0f);
    CHECK(ttm != (u32) -1);
    CHECK(ttm != wdw);
    KILL_CHUCKYA(ttm);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);

    // Back and forth: each respawned Chuckya gets its old slot back, dead.
    gCurrCourseNum = COURSE_WDW;
    wdw = get_unique_id(BINGO_UPDATE_KILLED_CHUCKYA, -2963.0f, 3840.0f, -3063.0f);
    KILL_CHUCKYA(wdw);
    gCurrCourseNum = COURSE_TTM;
    ttm = get_unique_id(BINGO_UPDATE_KILLED_CHUCKYA, -2676.0f, -2145.0f, 2923.0f);
    KILL_CHUCKYA(ttm);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    gCurrCourseNum = COURSE_THI;
    KILL_CHUCKYA(get_unique_id(BINGO_UPDATE_KILLED_CHUCKYA, -1800.0f, 2233.0f, -322.0f));
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
#undef KILL_CHUCKYA
    bingo_tracking_collectables_reset();
}

// The Big Chill Bully (SL) counts for the Bully tile like the Big Bully
// (LLL): both key on their level-macro spawn point via bhv_big_bully_init.
// Its death branch used to skip the report entirely. Mirrors the
// bully_act_level_death call; re-entering either course doesn't recount.
static void test_sim_big_chill_bully_counts(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    u32 big, chill;
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_KILL_BULLIES;
    o->data.collectableData.toGet = 2;
    o->data.collectableData.gotten = 0;

#define KILL_BULLY(uid)                                                   \
    do {                                                                  \
        if (is_new_kill(BINGO_UPDATE_KILLED_BULLY, (uid))) {              \
            bingo_update(BINGO_UPDATE_KILLED_BULLY);                      \
        }                                                                 \
    } while (0)

    gCurrCourseNum = COURSE_LLL;
    big = get_unique_id(BINGO_UPDATE_KILLED_BULLY, 0.0f, 307.0f, -4385.0f);
    CHECK(big != (u32) -1);
    KILL_BULLY(big);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    gCurrCourseNum = COURSE_SL;
    chill = get_unique_id(BINGO_UPDATE_KILLED_BULLY, 315.0f, 1331.0f, -4852.0f);
    CHECK(chill != (u32) -1);
    CHECK(chill != big);
    KILL_BULLY(chill);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);

    // Revisit: the respawned Chill Bully gets its old, dead slot back.
    o->state = BINGO_STATE_NONE;
    o->data.collectableData.toGet = 3;
    gCurrCourseNum = COURSE_LLL;
    KILL_BULLY(get_unique_id(BINGO_UPDATE_KILLED_BULLY, 0.0f, 307.0f, -4385.0f));
    gCurrCourseNum = COURSE_SL;
    chill = get_unique_id(BINGO_UPDATE_KILLED_BULLY, 315.0f, 1331.0f, -4852.0f);
    KILL_BULLY(chill);
    CHECK_EQ_INT(o->data.collectableData.gotten, 2);
#undef KILL_BULLY
    bingo_tracking_collectables_reset();
}

static void test_sim_coins_multiple_levels_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[3];
    struct MultiCourseCollectableData *d = &o->data.multiCourseCollectableData;
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_COINS_MULTIPLE_LEVELS;
    d->toGetEachCourse = 30;
    d->toGetTotal = 2;
    d->gottenTotal = 0;
    d->gottenThisCourse = 0;

    // 20 + 20 over two visits is not 30 in one visit.
    gCurrCourseNum = COURSE_BOB;
    gbCoinsJustGotten = 20;
    bingo_update(BINGO_UPDATE_COIN);
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(d->gottenTotal, 0);
    gbCoinsJustGotten = 10;
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(d->gottenTotal, 1);
    // The same course again adds nothing.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    gbCoinsJustGotten = 40;
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(d->gottenTotal, 1);
    // Castle and secret-course coins don't count.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    gCurrCourseNum = COURSE_NONE;
    bingo_update(BINGO_UPDATE_COIN);
    gCurrCourseNum = COURSE_PSS;
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(d->gottenTotal, 1);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    gCurrCourseNum = COURSE_WF;
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    bingo_tracking_collectables_reset();
}

static void test_sim_1ups_multiple_levels_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_1UPS_MULTIPLE_LEVELS;
    o->data.collectableData.toGet = 2;
    o->data.collectableData.gotten = 0;

    gCurrCourseNum = COURSE_BOB;
    bingo_update(BINGO_UPDATE_GOT_1UP);
    bingo_update(BINGO_UPDATE_GOT_1UP);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    gCurrCourseNum = COURSE_NONE;  // castle grounds 1-ups don't count
    bingo_update(BINGO_UPDATE_GOT_1UP);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    gCurrCourseNum = COURSE_THI;
    bingo_update(BINGO_UPDATE_GOT_1UP);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    bingo_tracking_collectables_reset();
}

// Coinless star: any coin in the target course fails the visit; coins
// elsewhere don't; re-entering resets; a clean visit completes it.
static void test_sim_coinless_star(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    o->type = BINGO_OBJECTIVE_STAR_COINLESS;
    o->data.starObjective.course = COURSE_WF;
    o->data.starObjective.starIndex = 1;

    // A coin in the castle or another course is fine.
    gCurrCourseNum = COURSE_BOB;
    gbCoinsJustGotten = 1;
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    // Any coin (a blue one here) in WF fails the visit.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    gCurrCourseNum = COURSE_WF;
    gbCoinsJustGotten = 5;
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(o->state, BINGO_STATE_FAILED_IN_THIS_COURSE);
    gbStarIndex = 1;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_FAILED_IN_THIS_COURSE);

    // Re-entering resets it; the wrong star doesn't complete it.
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    gbStarIndex = 2;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    // Pressing buttons is fine; the right star with no coin completes it.
    bingo_update(BINGO_UPDATE_A_PRESSED);
    gbStarIndex = 1;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);

    // Complete is final: a later coin doesn't undo it.
    bingo_update(BINGO_UPDATE_COIN);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
}

// The coinless pool never picks a 100-coin or red-coin star, nor the
// coin-line routes excluded in random_coinless_star.
static void test_coinless_star_pool(void) {
    u32 seed;
    int i;
    int seen = 0;
    for (seed = 1; seed <= 2000; seed++) {
        generate_board(seed);
        for (i = 0; i < 25; i++) {
            struct BingoObjective *o = &gBingoObjectives[i];
            s32 c, s;
            if (o->type != BINGO_OBJECTIVE_STAR_COINLESS) {
                continue;
            }
            seen++;
            c = o->data.starObjective.course;
            s = o->data.starObjective.starIndex;
            CHECK(c >= COURSE_BOB && c <= COURSE_RR);
            CHECK(s >= 0 && s <= 5);
            CHECK(!(c == COURSE_BOB && (s == 1 || s == 3 || s == 4)));
            CHECK(!(c == COURSE_SSL && s == 5));
            CHECK(!(c == COURSE_CCM && (s == 0 || s == 2 || s == 3)));
            CHECK(!(c == COURSE_TTM && (s == 2 || s == 3)));
            CHECK(!((c == COURSE_WF || c == COURSE_JRB || c == COURSE_BBH) && s == 3));
            CHECK(!(c == COURSE_HMC && s == 1));
            CHECK(!((c == COURSE_LLL || c == COURSE_DDD || c == COURSE_RR) && s == 2));
            CHECK(!((c == COURSE_SSL || c == COURSE_SL || c == COURSE_WDW || c == COURSE_THI) && s == 4));
            CHECK(!(c == COURSE_TTC && s == 5));
            if (gCurrentTestFailed) {
                printf("  (seed %u, cell %d, course %d, star %d)\n", seed, i, c, s);
                return;
            }
        }
    }
    CHECK(seen > 0);
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

static void test_sim_stars_in_level_k(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_star_reset();
    o->type = BINGO_OBJECTIVE_STARS_IN_LEVEL;
    o->data.courseCollectableData.course = 5;
    o->data.courseCollectableData.toGet = 3;
    o->data.courseCollectableData.gotten = 0;

    // Two stars in the course: progress, not complete.
    gCurrCourseNum = 5;
    bingo_set_star(4, 0);  // bingo_set_star indexes courses from 0
    bingo_update(BINGO_UPDATE_STAR);
    bingo_set_star(4, 1);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.courseCollectableData.gotten, 2);

    // A star in some other course does nothing.
    gCurrCourseNum = 6;
    bingo_set_star(5, 0);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    // The third star in the right course completes it (across visits).
    gCurrCourseNum = 5;
    bingo_set_star(4, 2);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
}

static void test_sim_stars_multiple_levels_k(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_star_reset();
    o->type = BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS;
    o->data.multiCourseCollectableData.toGetTotal = 2;
    o->data.multiCourseCollectableData.gottenTotal = 0;
    o->data.multiCourseCollectableData.toGetEachCourse = 2;
    o->data.multiCourseCollectableData.gottenThisCourse = 0;

    // One star each in three courses: no course reaches the per-course
    // count of 2, so nothing qualifies.
    gCurrCourseNum = 4;
    bingo_set_star(3, 0);
    bingo_update(BINGO_UPDATE_STAR);
    bingo_set_star(4, 0);
    bingo_update(BINGO_UPDATE_STAR);
    bingo_set_star(5, 0);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.multiCourseCollectableData.gottenTotal, 0);

    // A second star in one course: one course down.
    bingo_set_star(3, 1);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.multiCourseCollectableData.gottenTotal, 1);

    // A second star in another: that's 2 courses at 2 stars each.
    bingo_set_star(5, 1);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
}

static void test_sim_total_lives(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    o->type = BINGO_OBJECTIVE_LIVES;
    o->data.collectableData.toGet = 6;
    o->data.collectableData.gotten = 0;

    // The frame tick watches the HUD lives counter and feeds the
    // objective whenever it moves.
    gHudDisplay.lives = 4;
    bingo_update(BINGO_UPDATE_TIMER_FRAME_GLOBAL);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 4);

    // Down into the negatives: still nothing, and the climb restarts
    // from wherever the counter actually is.
    gHudDisplay.lives = -3;
    bingo_update(BINGO_UPDATE_TIMER_FRAME_GLOBAL);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(o->data.collectableData.gotten, -3);

    gHudDisplay.lives = 5;
    bingo_update(BINGO_UPDATE_TIMER_FRAME_GLOBAL);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    // Reaching the target number completes it.
    gHudDisplay.lives = 6;
    bingo_update(BINGO_UPDATE_TIMER_FRAME_GLOBAL);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
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

// Regression: EXIT GAME -> file select -> (maybe swap mode) -> pick a file
// re-runs setup_bingo_objectives on the same globals, with no memset in
// between. Completed cells, the race clock, and the star/collectable
// tracking used to leak into the new race, so the fresh board showed the
// old board's cells as complete (and objectives re-completed off stale
// counts) while the timer restarted from zero.
static void test_regeneration_resets_completion(void) {
    int i;
    generate_board(4242);

    // Simulate a played race: complete a row, bank tracking and the clock.
    for (i = 0; i < 5; i++) {
        gBingoObjectives[i * 5 + 2].state = BINGO_STATE_COMPLETE;
    }
    gbBingosCompleted = bingo_check_win();
    CHECK_EQ_INT(gbBingosCompleted, 1);
    bingo_set_star(5, 3);
    bingo_set_star(-1, 2);
    CHECK_EQ_INT(bingo_get_star_count(), 2);
    gbGlobalBingoTimer = 12345;
    gBingoStickyActNum[0] = 4;
    gCurrCourseNum = 2;
    CHECK(is_new_kill(BINGO_UPDATE_KILLED_GOOMBA,
                      get_unique_id(BINGO_UPDATE_KILLED_GOOMBA, 1.0f, 2.0f, 3.0f)));

    // The relaunch path, exactly as the game runs it: same seed, new mode,
    // no scrubbing of the globals beforehand.
    save_or_restore_weights();
    gbBingoMode = BINGO_MODE_LINE_1;
    setup_bingo_objectives(4242);

    for (i = 0; i < 25; i++) {
        CHECK_EQ_INT(gBingoObjectives[i].state, BINGO_STATE_NONE);
    }
    CHECK_EQ_INT(bingo_check_win(), 0);
    CHECK_EQ_INT(gbBingosCompleted, 0);
    CHECK_EQ_INT((s32) gbGlobalBingoTimer, 0);
    CHECK_EQ_INT(bingo_get_star_count(), 0);
    CHECK_EQ_INT(gBingoStickyActNum[0], 0);
    CHECK(peek_would_be_new_kill(BINGO_UPDATE_KILLED_GOOMBA,
                                 get_unique_id(BINGO_UPDATE_KILLED_GOOMBA, 1.0f, 2.0f, 3.0f)));
}

// Solo timeout: the race ends when the clock hits the limit, the timer
// freezes there, and a win at the buzzer still counts.
static void test_solo_timeout(void) {
    s32 i;
    reset_sim();
    gbBingoTimeout = 5;  // minutes
    gbGlobalBingoTimer = 0;
    CHECK_EQ_INT(bingo_race_timed_out(), 0);
    CHECK_EQ_INT(bingo_race_over(), 0);

    // One frame before the limit: still racing.
    gbGlobalBingoTimer = bingo_timeout_frames() - 1;
    bingo_update(BINGO_UPDATE_TIMER_FRAME_GLOBAL);
    CHECK_EQ_INT(bingo_race_timed_out(), 1);  // ...and that tick hit it
    CHECK_EQ_INT(bingo_race_over(), 1);

    // The timer froze at the limit.
    for (i = 0; i < 10; i++) {
        bingo_update(BINGO_UPDATE_TIMER_FRAME_GLOBAL);
    }
    CHECK_EQ_INT((s32) gbGlobalBingoTimer, bingo_timeout_frames());

    // A board won before the buzzer is a win, not a timeout.
    reset_sim();
    gbBingoTimeout = 5;
    gbBingosCompleted = 1;
    gbGlobalBingoTimer = bingo_timeout_frames() + 100;
    CHECK_EQ_INT(bingo_race_timed_out(), 0);
    CHECK_EQ_INT(bingo_race_over(), 1);

    gbBingoTimeout = 0;  // leave the option off for the other tests
    gbGlobalBingoTimer = 0;
}

// ---------------------------------------------------------------------------
// Presets: each one must round-trip through the match function and must
// always be able to fill a board — no disabled type may appear in any
// cell, even where a preset starves a difficulty class down to a few
// entries (the generator's uniform fallback has to cover those draws).

// ---------------------------------------------------------------------------
// Progression objectives (unlock OFF only).

// Open cannons: one per course, keyed by course; the same Buddy (or a
// second talk in the same course) never counts twice.
static void test_sim_open_cannons_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_collectables_reset();
    o->type = BINGO_OBJECTIVE_OPEN_CANNONS;
    o->data.collectableData.toGet = 3;

    CHECK_EQ_INT(bingo_track_cannon_opened(COURSE_BOB), 1);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    CHECK_EQ_INT(bingo_track_cannon_opened(COURSE_BOB), 0);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    // Other collectable events don't count, and course changes keep it.
    bingo_update(BINGO_UPDATE_CANNON_COLLECTABLE);
    bingo_update(BINGO_UPDATE_COURSE_CHANGED);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    // The key is the course passed in, not gCurrCourseNum.
    gCurrCourseNum = COURSE_BOB;
    CHECK_EQ_INT(bingo_track_cannon_opened(COURSE_CCM), 1);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    CHECK_EQ_INT(bingo_track_cannon_opened(COURSE_WMOTR), 1);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(o->data.collectableData.gotten, 3);
    bingo_tracking_collectables_reset();
}

// Toad stars: castle stars 0-2 (bingo_set_star(-1, i)); MIPS's (3, 4)
// and course stars don't count.
static void test_sim_toad_stars_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_star_reset();
    o->type = BINGO_OBJECTIVE_TOAD_STARS;
    o->data.collectableData.toGet = 2;

    bingo_set_star(COURSE_BOB - 1, 0);
    bingo_update(BINGO_UPDATE_STAR);
    bingo_set_star(-1, 3);  // MIPS 1
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->data.collectableData.gotten, 0);

    bingo_set_star(-1, 0);  // basement Toad
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);
    bingo_set_star(-1, 0);  // same star again
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->data.collectableData.gotten, 1);

    bingo_set_star(-1, 2);  // top-floor Toad
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    bingo_tracking_star_reset();
}

static void test_sim_mips_objective(void) {
    struct BingoObjective *o = &gBingoObjectives[0];
    reset_sim();
    bingo_tracking_star_reset();
    o->type = BINGO_OBJECTIVE_MIPS;
    o->data.collectableData.toGet = 1;

    bingo_set_star(-1, 0);  // a Toad star isn't MIPS
    bingo_update(BINGO_UPDATE_STAR);
    bingo_set_star(COURSE_HMC - 1, 3);
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_NONE);

    bingo_set_star(-1, 4);  // either MIPS star counts (the 50-star one here)
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(o->state, BINGO_STATE_COMPLETE);
    bingo_tracking_star_reset();
}

static int count_progression_cells(void) {
    int i, n = 0;
    for (i = 0; i < 25; i++) {
        n += bingo_objective_needs_unlock_off(gBingoObjectives[i].type) ? 1 : 0;
    }
    return n;
}

// The gate: unlock ON never deals a progression objective, unlock OFF does
// (in every class they have weights in), and unlock OFF boards still meet
// the sweep's invariants.
static void test_unlock_gate(void) {
    static s32 seen[BINGO_OBJECTIVE_TOTAL_AMOUNT][4];
    u32 seed;
    int i, onCells = 0, offBoards = 0, dupBoards = 0;

    memset(seen, 0, sizeof(seen));
    gBingoFullGameUnlocked = 1;
    for (seed = 1; seed <= 1000; seed++) {
        generate_board(seed);
        onCells += count_progression_cells();
    }
    CHECK_EQ_INT(onCells, 0);

    gBingoFullGameUnlocked = 0;
    for (seed = 1; seed <= 1000; seed++) {
        generate_board(seed);
        offBoards += count_progression_cells() > 0;
        dupBoards += board_has_line_duplicates();
        for (i = 0; i < 25; i++) {
            struct BingoObjective *o = &gBingoObjectives[i];
            CHECK(o->initialized);
            CHECK(o->title[0] != '\0');
            seen[o->type][o->class]++;
            if (o->type == BINGO_OBJECTIVE_OPEN_CANNONS) {
                CHECK(o->data.collectableData.toGet >= 3 && o->data.collectableData.toGet <= 5);
            } else if (o->type == BINGO_OBJECTIVE_TOAD_STARS || o->type == BINGO_OBJECTIVE_MIPS) {
                CHECK_EQ_INT(o->data.collectableData.toGet, 1);
            }
        }
        if (gCurrentTestFailed) {
            printf("  (seed %u)\n", seed);
            break;
        }
    }
    printf("  unlock OFF: %d of 1000 boards have a progression cell\n", offBoards);
    CHECK(offBoards > 100);
    CHECK_EQ_INT(dupBoards, EXPECTED_BOARDS_WITH_DUPLICATES);
    CHECK(seen[BINGO_OBJECTIVE_OPEN_CANNONS][BINGO_CLASS_EASY] > 0);
    CHECK(seen[BINGO_OBJECTIVE_OPEN_CANNONS][BINGO_CLASS_MEDIUM] > 0);
    CHECK(seen[BINGO_OBJECTIVE_OPEN_CANNONS][BINGO_CLASS_HARD] > 0);
    CHECK(seen[BINGO_OBJECTIVE_TOAD_STARS][BINGO_CLASS_HARD] > 0);
    CHECK(seen[BINGO_OBJECTIVE_MIPS][BINGO_CLASS_HARD] > 0);

    // Heavy mask: only the progression types enabled. Unlock OFF deals
    // nothing else (the uniform fallback covers classes without them);
    // unlock ON treats them as disabled, so the all-disabled fallback
    // (plain STAR) is what comes out.
    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
        gBingoObjectivesDisabled[i] = !bingo_objective_needs_unlock_off(i);
    }
    for (seed = 1; seed <= 50; seed++) {
        generate_board(seed);
        CHECK_EQ_INT(count_progression_cells(), 25);
    }
    gBingoFullGameUnlocked = 1;
    for (seed = 1; seed <= 50; seed++) {
        generate_board(seed);
        CHECK_EQ_INT(count_progression_cells(), 0);
    }

    memset(gBingoObjectivesDisabled, 0, sizeof(gBingoObjectivesDisabled));
    gBingoFullGameUnlocked = 1;
}

// Call and Response: the queue is a permutation of the board, ramps
// easy -> medium/center -> hard, and is a pure function of the seed.
static void test_calls_queue(void) {
    u32 seed;
    s32 clashes = 0, pairs = 0;
    for (seed = 1; seed <= 500; seed++) {
        u8 first[25];
        s32 seen[25] = { 0 };
        s32 i, prevTier = 0;
        generate_board(seed);
        memcpy(first, gBingoCallQueue, 25);
        for (i = 0; i < 25; i++) {
            enum BingoObjectiveClass c = gBingoObjectives[gBingoCallQueue[i]].class;
            s32 tier = c == BINGO_CLASS_EASY ? 0 : c == BINGO_CLASS_HARD ? 2 : 1;
            seen[gBingoCallQueue[i]]++;
            CHECK(tier >= prevTier);
            prevTier = tier;
            if (i > 0) {
                struct BingoObjective *a = &gBingoObjectives[gBingoCallQueue[i - 1]];
                struct BingoObjective *b = &gBingoObjectives[gBingoCallQueue[i]];
                if (is_star_type(a->type) && is_star_type(b->type)) {
                    pairs++;
                    clashes += a->data.starObjective.course == b->data.starObjective.course;
                }
            }
        }
        for (i = 0; i < 25; i++) {
            CHECK_EQ_INT(seen[i], 1);
        }
        bingo_calls_build_queue(seed);
        CHECK(memcmp(first, gBingoCallQueue, 25) == 0);
    }
    // Consecutive star calls almost never share a course (only when a
    // tier runs out of other courses).
    printf("  calls: %d of %d consecutive star calls share a course\n", clashes, pairs);
    CHECK(clashes * 50 < pairs);
}

// Only the open calls see events, a call that opens mid-update does not
// also collect that update, and the call target wins a solo race.
static void test_calls_gating_and_win(void) {
    s32 i;
    reset_sim();
    gbBingoMode = BINGO_MODE_CALLS;
    gBingoCallsOpen = 2;
    gBingoCallsToWin = 3;
    for (i = 0; i < 25; i++) {
        gBingoCallQueue[i] = i;
        gBingoCellClaimers[i] = 0;
    }
    for (i = 0; i < 5; i++) {
        gBingoObjectives[i].type = BINGO_OBJECTIVE_STAR;
        gBingoObjectives[i].data.starObjective.course = 1;
        gBingoObjectives[i].data.starObjective.starIndex = i;
    }
    // Cell 2 wants the same star as cell 0, but is not called yet.
    gBingoObjectives[2].data.starObjective.starIndex = 0;
    CHECK_EQ_INT(bingo_calls_open_mask(), 0x3);

    // A closed call ignores its star.
    gCurrCourseNum = 1;
    gbStarIndex = 3;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(gBingoObjectives[3].state, BINGO_STATE_NONE);

    // Star 0 wins call 0; call 2 opens but the same star must not win it.
    gbStarIndex = 0;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(gBingoObjectives[0].state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(gBingoObjectives[2].state, BINGO_STATE_NONE);
    CHECK_EQ_INT(bingo_calls_open_mask(), 0x6);

    // An opponent's claim closes a call too (online exclusive claim).
    gBingoCellClaimers[1] = 1u << 2;
    CHECK_EQ_INT(bingo_calls_open_mask(), 0xC);

    gbStarIndex = 0;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(gBingoObjectives[2].state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(bingo_race_won(), 0);
    gbStarIndex = 3;
    bingo_update(BINGO_UPDATE_STAR);
    CHECK_EQ_INT(gBingoObjectives[3].state, BINGO_STATE_COMPLETE);
    CHECK_EQ_INT(bingo_race_won(), 1);
    CHECK_EQ_INT(bingo_race_over(), 1);

    for (i = 0; i < 25; i++) {
        gBingoCellClaimers[i] = 0;
    }
    gbBingoMode = BINGO_MODE_LINE_1;
}

static void test_presets_generate_clean_boards(void) {
    s32 p;
    u32 seed;
    int i;

    // Types past 63 live in the second mask word: SRL must enable the
    // castle secret stars (64) and still disable everything off-list,
    // including the last type.
    bingo_preset_apply(BINGO_PRESET_SRL);
    CHECK_EQ_INT(BINGO_OBJECTIVE_CASTLE_SECRET_STARS >= 64, 1);
    CHECK_EQ_INT(gBingoObjectivesDisabled[BINGO_OBJECTIVE_CASTLE_SECRET_STARS], 0);
    CHECK_EQ_INT(gBingoObjectivesDisabled[BINGO_OBJECTIVE_HUNDRED_COIN_STARS], 0);
    CHECK_EQ_INT(gBingoObjectivesDisabled[BINGO_OBJECTIVE_SPLATOON], 1);
    CHECK_EQ_INT(gBingoObjectivesDisabled[BINGO_OBJECTIVE_MIPS], 1);
    gBingoObjectivesDisabled[BINGO_OBJECTIVE_CASTLE_SECRET_STARS] = 1;
    CHECK_EQ_INT(bingo_preset_current(), -1);
    bingo_preset_apply(BINGO_PRESET_VANILLA);
    CHECK_EQ_INT(gBingoObjectivesDisabled[BINGO_OBJECTIVE_CASTLE_SECRET_STARS], 0);

    for (p = 0; p < BINGO_PRESET_COUNT; p++) {
        bingo_preset_apply(p);
        CHECK_EQ_INT(bingo_preset_current(), p);
        for (seed = 1; seed <= 200; seed++) {
            generate_board(seed);
            for (i = 0; i < 25; i++) {
                struct BingoObjective *o = &gBingoObjectives[i];
                CHECK(o->initialized);
                CHECK(!gBingoObjectivesDisabled[o->type]);
                CHECK(bingo_objective_eligible(o->type));
                if (gCurrentTestFailed) {
                    printf("  (preset %d, seed %u, cell %d, type %d)\n",
                           (int) p, seed, i, o->type);
                    return;
                }
            }
        }
    }

    // Restore boot defaults for the other tests.
    memset(gBingoObjectivesDisabled, 0, sizeof(gBingoObjectivesDisabled));
    gBingoFullGameUnlocked = 1;
    gbBingoMode = BINGO_MODE_LINE_1;
    CHECK_EQ_INT(bingo_preset_current(), -1);
}

int main(void) {
    // BOARD_SEED=n prints that board instead of running tests. Handy for
    // comparing against what the ROM shows on screen, and used as the
    // oracle by web/check.mjs. BOARD_TARGET=n (1, 2, 3, or 12),
    // BOARD_DISABLE=t1,t2,... (objective type numbers) and BOARD_UNLOCK=0|1
    // set the same options the file select screen offers.
    const char *seedArg = getenv("BOARD_SEED");
    if (seedArg != NULL) {
        const char *targetArg = getenv("BOARD_TARGET");
        const char *disableArg = getenv("BOARD_DISABLE");
        // BOARD_UNLOCK=0 plays "Unlock full game" OFF (default ON, as at
        // boot): the only setting that makes progression objectives eligible.
        const char *unlockArg = getenv("BOARD_UNLOCK");
        if (unlockArg != NULL) {
            gBingoFullGameUnlocked = (u8) (strtol(unlockArg, NULL, 10) != 0);
        }
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
            // generate_board pins the mode to LINE_1; redo setup with the
            // requested target, exactly like a fresh boot with that option.
            save_or_restore_weights();
            memset(gBingoObjectives, 0, sizeof(gBingoObjectives));
            gbBingoMode = mode_from_target((s32) strtol(targetArg, NULL, 10));
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
    RUN_TEST(test_weighting_expect);
    RUN_TEST(test_invariant_sweep);
    RUN_TEST(test_weight_budget);
    RUN_TEST(test_weighted_pick_skips_exhausted);
    RUN_TEST(test_repeated_generation_resets_budgets);
    RUN_TEST(test_sim_single_star);
    RUN_TEST(test_sim_coin_objective);
    RUN_TEST(test_sim_cannon_stars_objective);
    RUN_TEST(test_sim_red_coin_stars_objective);
    RUN_TEST(test_sim_hundred_coin_stars_objective);
    RUN_TEST(test_sim_castle_secret_stars_objective);
    RUN_TEST(test_sim_splatoon_objective);
    RUN_TEST(test_sim_unique_deaths);
    RUN_TEST(test_sim_random_stars_objective);
    RUN_TEST(test_sim_kill_collectable);
    RUN_TEST(test_sim_blue_coin_objective);
    RUN_TEST(test_sim_warp_pads_objective);
    RUN_TEST(test_sim_koopa_shells_objective);
    RUN_TEST(test_sim_spin_hearts_objective);
    RUN_TEST(test_sim_caps_worn_objective);
    RUN_TEST(test_sim_purple_switches_objective);
    RUN_TEST(test_sim_stuck_in_ground_objective);
    RUN_TEST(test_sim_blj_cell0_alternating_courses);
    RUN_TEST(test_sim_chuckya_alternating_courses);
    RUN_TEST(test_sim_big_chill_bully_counts);
    RUN_TEST(test_sim_coins_multiple_levels_objective);
    RUN_TEST(test_sim_1ups_multiple_levels_objective);
    RUN_TEST(test_sim_coinless_star);
    RUN_TEST(test_coinless_star_pool);
    RUN_TEST(test_sim_abz_fail_and_reset);
    RUN_TEST(test_sim_timed_star);
    RUN_TEST(test_sim_stars_in_level_k);
    RUN_TEST(test_sim_stars_multiple_levels_k);
    RUN_TEST(test_sim_total_lives);
    RUN_TEST(test_sim_row_completion_wins);
    RUN_TEST(test_win_detection);
    RUN_TEST(test_regeneration_resets_completion);
    RUN_TEST(test_solo_timeout);
    RUN_TEST(test_sim_open_cannons_objective);
    RUN_TEST(test_sim_toad_stars_objective);
    RUN_TEST(test_sim_mips_objective);
    RUN_TEST(test_unlock_gate);
    RUN_TEST(test_presets_generate_clean_boards);
    RUN_TEST(test_calls_queue);
    RUN_TEST(test_calls_gating_and_win);
    return test_summary();
}
