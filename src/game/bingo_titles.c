#include <stdio.h>

#include <ultra64.h>
#include <PR/os_libc.h>

#include "area.h"
#include "bingo.h"

#include "bingo_descriptions.h"
#include "bingo_const.h"
#include "bingo_objective_func.h"
#include "bingo_objective_init.h"
#include "bingo_titles.h"
#include "segment2.h"
#include "strcpy.h"
#include "level_table.h"

#define TEXT_FILLED_STAR 0xFA

void get_course_abbreviation(enum CourseNum course, char *abbrev) {
    // TODO: Avoid buffer overflows
    strcpy(abbrev, courseAbbreviations[course - 1]); // offset since BOB = 1
}

////////////////////////////////////////////////////////////////////////////////

void get_star_objective_title(struct BingoObjective *objective) {
    enum CourseNum course = objective->data.starObjective.course;
    s32 star = objective->data.starObjective.starIndex;
    char abbrev[9] = { 0xFF };
    char PSSname[5] = { 0xFF };
    get_course_abbreviation(course, &abbrev);

    // We do "star + 1" here to make it human-readable.
    if (COURSE_IS_MAIN_COURSE(course)) {
        sprintf(objective->title, "%s%c%d", abbrev, TEXT_FILLED_STAR, star + 1);
    } else if (course == COURSE_PSS) {
        switch (star) {
            case PSS_STAR_FAST:
                strcpy(PSSname, "21s");
                break;
            case PSS_STAR_SLOW:
                strcpy(PSSname, "BOX");
                break;
        }
        sprintf(objective->title, "%s %s", abbrev, PSSname);
    } else {
        sprintf(objective->title, "%s%c", abbrev, TEXT_FILLED_STAR);
    }
}

void get_coin_objective_title(struct BingoObjective *objective) {
    enum CourseNum course = objective->data.courseCollectableData.course;
    s32 coins = objective->data.courseCollectableData.toGet;
    char abbrev[9];
    get_course_abbreviation(course, &abbrev);

    if (strlen(abbrev) >= 4) {
        sprintf(objective->title, "%s%d", abbrev, coins);
    } else {
        sprintf(objective->title, "%s %d", abbrev, coins);
    }
}

void get_multicoin_objective_title(struct BingoObjective *objective) {
    sprintf(objective->title, "%d", objective->data.collectableData.toGet);
}

void get_in_level_objective_title(struct BingoObjective *objective) {
    char abbrev[9];
    get_course_abbreviation(objective->data.courseCollectableData.course, &abbrev);

    sprintf(objective->title, "%s %d", abbrev, objective->data.courseCollectableData.toGet);
}

void get_bowser_objective_title(struct BingoObjective *objective) {
    char abbrev[9] = { 0xFF };
    enum LevelNum level = objective->data.levelData.level;
    if (level == LEVEL_BOWSER_1) {
        get_course_abbreviation(COURSE_BITDW, &abbrev);
    } else if (level == LEVEL_BOWSER_2) {
        get_course_abbreviation(COURSE_BITFS, &abbrev);
    } else if (level == LEVEL_BOWSER_3) {
        get_course_abbreviation(COURSE_BITS, &abbrev);
    }

    sprintf(objective->title, "%s", abbrev);
}

void get_roof_without_cannon_objective_title(struct BingoObjective *objective) {
    sprintf(objective->title, "x1");
}

void get_collectable_objective_title(struct BingoObjective *objective) {
    sprintf(objective->title, "x%d", objective->data.collectableData.toGet);
}

void get_dangerous_wallkicks_title(struct BingoObjective *objective) {
    struct MultiCourseCollectableData *data = &objective->data.multiCourseCollectableData;
    sprintf(objective->title, "%dx %dc", data->toGetEachCourse, data->toGetTotal);
}

void get_stars_multiple_levels_title(struct BingoObjective *objective) {
    struct MultiCourseCollectableData *data = &objective->data.multiCourseCollectableData;
    if (data->toGetEachCourse == 1) {
        // The classic 1-of-N form keeps its familiar title.
        sprintf(objective->title, "x%d", data->toGetTotal);
    } else {
        sprintf(objective->title, "%dx %dc", data->toGetEachCourse, data->toGetTotal);
    }
}

////////////////////////////////////////////////////////////////////////////////

void get_objective_title(struct BingoObjective *objective) {
    switch (objective->type) {
        // TODO: Get these for free by being a star:
        case BINGO_OBJECTIVE_STAR:
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_TIMED:
        case BINGO_OBJECTIVE_STAR_TTC_RANDOM:
        case BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK:
        case BINGO_OBJECTIVE_STAR_GREEN_DEMON:
        case BINGO_OBJECTIVE_STAR_CLICK_GAME:
        case BINGO_OBJECTIVE_STAR_DAREDEVIL:
        case BINGO_OBJECTIVE_RANDOM_RED_COINS:
            get_star_objective_title(objective);
            break;
        case BINGO_OBJECTIVE_COIN:
        case BINGO_OBJECTIVE_SPLATOON:
            get_coin_objective_title(objective);
            break;
        case BINGO_OBJECTIVE_MULTICOIN:
            get_multicoin_objective_title(objective);
            break;
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
            get_in_level_objective_title(objective);
            break;
        case BINGO_OBJECTIVE_BOWSER:
            get_bowser_objective_title(objective);
            break;
        // TODO: Get these for free by being a collectable:
        case BINGO_OBJECTIVE_LOSE_MARIO_HAT:
        case BINGO_OBJECTIVE_UNIQUE_DEATHS:
        case BINGO_OBJECTIVE_BLJ:
        case BINGO_OBJECTIVE_RACING_STARS:
        case BINGO_OBJECTIVE_SECRETS_STARS:
        case BINGO_OBJECTIVE_MULTISTAR:
        case BINGO_OBJECTIVE_EXCLAMATION_MARK_BOX:
        case BINGO_OBJECTIVE_WING_CAP_BOX:
        case BINGO_OBJECTIVE_VANISH_CAP_BOX:
        case BINGO_OBJECTIVE_METAL_CAP_BOX:
        case BINGO_OBJECTIVE_SIGNPOST:
        case BINGO_OBJECTIVE_POLES:
        case BINGO_OBJECTIVE_SHOOT_CANNONS:
        case BINGO_OBJECTIVE_RED_COIN:
        case BINGO_OBJECTIVE_AMPS:
        case BINGO_OBJECTIVE_KILL_GOOMBAS:
        case BINGO_OBJECTIVE_KILL_BOBOMBS:
        case BINGO_OBJECTIVE_KILL_SPINDRIFTS:
        case BINGO_OBJECTIVE_KILL_MR_IS:
        case BINGO_OBJECTIVE_KILL_SCUTTLEBUGS:
        case BINGO_OBJECTIVE_KILL_BULLIES:
        case BINGO_OBJECTIVE_KILL_CHUCKYAS:
            get_collectable_objective_title(objective);
            break;
        case BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS:
            get_dangerous_wallkicks_title(objective);
            break;
        case BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS:
            get_stars_multiple_levels_title(objective);
            break;
        case BINGO_OBJECTIVE_ROOF_WITHOUT_CANNON:
            get_roof_without_cannon_objective_title(objective);
            break;
    }
}