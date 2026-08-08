#ifndef _BINGO_CONST_H
#define _BINGO_CONST_H

#include <ultra64.h>

#include "course_table.h"

struct ABCData {
    enum CourseNum course;
    s32 star;
    char hint[100];
};

extern char *courseAbbreviations[24];
extern struct ABCData possibleABC[];
extern s32 numPossibleABC;

s32 get_1ups_in_level(enum CourseNum course);
s32 get_floors_in_course(enum CourseNum course);
s32 get_time_for_star(enum CourseNum course, s32 star);

struct ClickInfo {
    enum CourseNum course;
    s8 star;  // do not use: it's 1-indexed! just for documentation
    s8 minClicks;
    s8 maxClicks;
};

#define CLICK_GAME_STAR_BANNED -1
#define CLICK_GAME_MAX_IS_MIN -1
struct ClickInfo *get_click_info_for_star(enum CourseNum course, s32 star);

#endif /* _BINGO_CONST_H */
