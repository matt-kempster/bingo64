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
// 8 * (15 + 3 + 3 + 1 + 1) = 184 (Bowser, Cap Courses, SA, WMoTR)
#define MAX_RED_COINS 200

#define TOTAL_UIDS ( \
    MAX_GOOMBAS \
    + MAX_BOBOMBS \
    + MAX_EXCLAMATION_BOXES \
    + MAX_RED_COINS \
    ) + 1

// I really hope nothing is actually at (0, 0, 0)....
struct UID sIDTable[TOTAL_UIDS] = { { 0 } };

void get_index_range(enum BingoObjectiveUpdate update, s32 *start, s32 *length) {
    enum BingoObjectiveUpdate i;
    s32 rangeLength = 0;
    s32 prevRangeLength = 0;

    *start = 0;
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