#include <ultra64.h>
#include <string.h>

#include "bingo.h"
#include "engine/rand.h"
#include "harness.h"

void setup_bingo_objectives(u32 seed);
u8 bingo_check_win(void);

// The board setup code keeps "how many times can this objective still be
// used" counters inside its weight tables. They change while a board is
// made. To act like a fresh boot, we save the tables once at startup and
// put them back before every board we generate.
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

static void test_boards_are_fully_initialized(void) {
    u32 seed;
    int i;
    for (seed = 1; seed <= 200; seed++) {
        generate_board(seed);
        for (i = 0; i < 25; i++) {
            CHECK(gBingoObjectives[i].initialized);
            CHECK(gBingoObjectives[i].title[0] != '\0');
            if (gCurrentTestFailed) {
                printf("  (seed %u, cell %d)\n", seed, i);
                return;
            }
        }
    }
}

static void test_win_detection(void) {
    int i;
    generate_board(777);
    CHECK_EQ_INT(bingo_check_win(), 0);

    // Complete the middle row. Note the board is stored column-major
    // for win checking: index i + j * 5 is column i, row j.
    for (i = 0; i < 5; i++) {
        gBingoObjectives[i * 5 + 2].state = BINGO_STATE_COMPLETE;
    }
    CHECK_EQ_INT(bingo_check_win(), 1);

    // Complete the main diagonal too: two bingos now.
    for (i = 0; i < 5; i++) {
        gBingoObjectives[i * 5 + i].state = BINGO_STATE_COMPLETE;
    }
    CHECK_EQ_INT(bingo_check_win(), 2);
}

int main(void) {
    RUN_TEST(test_mt19937_reference);
    RUN_TEST(test_same_seed_same_board);
    RUN_TEST(test_different_seed_different_board);
    RUN_TEST(test_boards_are_fully_initialized);
    RUN_TEST(test_win_detection);
    return test_summary();
}
