#include <ultra64.h>
#include "types.h"
#include "area.h"
#include "bingo_tracking_collectables.h"

struct UID {
    enum CourseNum course;
    Vec3f pos;
    u8 killed;
};

// The bestiary is the best resource for enemy totals:
// https://gamefaqs.gamespot.com/n64/198848-super-mario-64/faqs/54401

// Meanwhile the item chart is the best resource for items:
// http://www.sm64.com/itemchart.html

// I think there are 77 Goombas or something like that.
#define MAX_GOOMBAS 90
// Should be 30 Bob-ombs.
#define MAX_BOBOMBS 40
// sm64.com says 65 ! boxes.
#define MAX_EXCLAMATION_BOXES 70

#define MAX_WING_CAP_BOXES 15
#define MAX_VANISH_CAP_BOXES 9
#define MAX_METAL_CAP_BOXES 14

// 8 * (15 + 3 + 3 + 1 + 1) = 184 (Bowser, Cap Courses, SA, WMoTR)
#define MAX_RED_COINS 200
// There are 6 in the game
#define MAX_MR_IS 10
// There are 9 in the game
#define MAX_SCUTTLEBUGS 15
// There are 16 (not including the chill bully)
#define MAX_BULLIES 19
// There are 5
#define MAX_CHUCKYAS 5
// There are 19
#define MAX_SPINDRIFTS 25
// There are 83
#define MAX_SIGNPOSTS 100
// Well, there are 25 courses, and the objective could show
// up 2 times... 25 * 2
// This is really inefficient and also fragile. This should use
// a different type of memorization system.
#define MAX_COURSES_WALLKICKS 50
#define MAX_COURSES_BLJ 25
// There are 24
#define MAX_AMPS 25
// There are 32 regular poles, 1 giant pole in WF,
// 9 poles in DDD, 4 moving ones in BitFS.
#define MAX_POLES 50
// There are 17
#define MAX_CANNONS 20
// There are 13
#define MAX_RECOVERY_HEARTS 13

#define TOTAL_UIDS ( \
        MAX_GOOMBAS \
        + MAX_BOBOMBS \
        + MAX_EXCLAMATION_BOXES \
        + MAX_WING_CAP_BOXES \
        + MAX_VANISH_CAP_BOXES \
        + MAX_METAL_CAP_BOXES \
        + MAX_RED_COINS \
        + MAX_MR_IS \
        + MAX_SCUTTLEBUGS \
        + MAX_BULLIES \
        + MAX_CHUCKYAS \
        + MAX_SPINDRIFTS \
        + MAX_SIGNPOSTS \
        + MAX_COURSES_WALLKICKS \
        + MAX_COURSES_BLJ \
        + MAX_AMPS \
        + MAX_POLES \
        + MAX_CANNONS \
        + MAX_RECOVERY_HEARTS \
    ) + 2

// I really hope nothing is actually at (0, 0, 0)....
struct UID sIDTable[TOTAL_UIDS] = { { 0 } };

void get_index_range(enum BingoObjectiveUpdate update, s32 *start, s32 *length) {
    enum BingoObjectiveUpdate i;
    s32 rangeLength = 0;
    s32 prevRangeLength = 0;

    *start = 1;  // Avoid UID 0.
    for (i = BINGO_COLLECTABLES_MIN; i <= BINGO_COLLECTABLES_MAX; i++) {
        prevRangeLength = rangeLength;
        switch (i) {
            case BINGO_UPDATE_KILLED_GOOMBA:
                rangeLength = MAX_GOOMBAS;
                break;
            case BINGO_UPDATE_KILLED_BOBOMB:
                rangeLength = MAX_BOBOMBS;
                break;
            case BINGO_UPDATE_EXCLAMATION_MARK_BOX:
                rangeLength = MAX_EXCLAMATION_BOXES;
                break;
            case BINGO_UPDATE_WING_CAP_BOX:
                rangeLength = MAX_WING_CAP_BOXES;
                break;
            case BINGO_UPDATE_VANISH_CAP_BOX:
                rangeLength = MAX_VANISH_CAP_BOXES;
                break;
            case BINGO_UPDATE_METAL_CAP_BOX:
                rangeLength = MAX_METAL_CAP_BOXES;
                break;
            case BINGO_UPDATE_RED_COIN:
                rangeLength = MAX_RED_COINS;
                break;
            case BINGO_UPDATE_KILLED_SPINDRIFT:
                rangeLength = MAX_SPINDRIFTS;
                break;
            case BINGO_UPDATE_KILLED_MR_I:
                rangeLength = MAX_MR_IS;
                break;
            case BINGO_UPDATE_KILLED_SCUTTLEBUG:
                rangeLength = MAX_SCUTTLEBUGS;
                break;
            case BINGO_UPDATE_KILLED_BULLY:
                rangeLength = MAX_BULLIES;
                break;
            case BINGO_UPDATE_KILLED_CHUCKYA:
                rangeLength = MAX_CHUCKYAS;
                break;
            case BINGO_UPDATE_READ_SIGNPOST:
                rangeLength = MAX_SIGNPOSTS;
                break;
            case BINGO_UPDATE_DANGEROUS_WALL_KICK:
                rangeLength = MAX_COURSES_WALLKICKS;
                break;
            case BINGO_UPDATE_BLJ:
                rangeLength = MAX_COURSES_BLJ;
                break;
            case BINGO_UPDATE_ZAPPED_BY_AMP:
                rangeLength = MAX_AMPS;
                break;
            case BINGO_UPDATE_GRABBED_POLE:
                rangeLength = MAX_POLES;
                break;
            case BINGO_UPDATE_CANNON_COLLECTABLE:
                rangeLength = MAX_CANNONS;
                break;
            case BINGO_UPDATE_RECOVERY_HEART:
                rangeLength = MAX_RECOVERY_HEARTS;
                break;
        }
        *start += prevRangeLength;
        if (i == update) {
            *length = rangeLength;
            return;
        }
    }
    // Shouldn't reach here.
    *length = 0;
}

u32 get_unique_id(enum BingoObjectiveUpdate type, f32 posX, f32 posY, f32 posZ) {
    u32 j;
    struct UID *uid;
    s32 idxStart, idxLength = 0;
    get_index_range(type, &idxStart, &idxLength);

    if (idxLength == 0) {
        return -1;
    }

    for (j = idxStart; j < idxStart + idxLength; j++) {
        uid = &sIDTable[j];
        if (
            uid->course == gCurrCourseNum
            && uid->pos[0] == posX && uid->pos[1] == posY && uid->pos[2] == posZ
        ) {
            return j;
        }

        if (uid->pos[0] == 0 && uid->pos[1] == 0 && uid->pos[2] == 0) {
            uid->course = gCurrCourseNum;
            uid->pos[0] = posX;
            uid->pos[1] = posY;
            uid->pos[2] = posZ;
            uid->killed = 0;
            return j;
        }
    }
    // No empty slots somehow?!
    return -1;
}

s32 is_new_kill(enum BingoObjectiveUpdate type, u32 uid) {
    u8 killed;
    s32 idxStart, idxLength = 0;
    get_index_range(type, &idxStart, &idxLength);

    if (idxLength == 0) {
        return 1;
    }

    killed = sIDTable[uid].killed;
    if (killed) {
        return 0;
    } else {
        sIDTable[uid].killed = 1;
        return 1;
    }
}

s32 peek_would_be_new_kill(enum BingoObjectiveUpdate type, u32 uid) {
    u8 killed;
    s32 idxStart, idxLength = 0;
    get_index_range(type, &idxStart, &idxLength);

    if (idxLength == 0) {
        return 1;
    }

    killed = sIDTable[uid].killed;
    if (killed) {
        return 0;
    } else {
        return 1;
    }

}