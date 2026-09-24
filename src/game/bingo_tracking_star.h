#ifndef _BINGO_TRACKING_STAR_H
#define _BINGO_TRACKING_STAR_H

#include <ultra64.h>
#include "area.h"

extern u32 gbCourseStars[25];
// Castle stars (course NONE) by star index: bits 0-2 are the Toads' stars
// (12 / 25 / 35 star gates), bits 3-4 MIPS's (15 / 50).
extern u32 gbSecretStarFlags;
#define BINGO_SECRET_FLAGS_TOAD 0x07
#define BINGO_SECRET_FLAGS_MIPS 0x18

void bingo_tracking_star_reset(void);
void bingo_set_star(s16 course, s16 star);
s32 bingo_get_course_count(enum CourseNum course);
s32 bingo_get_star_count(void);
u8 bingo_get_course_flags(s16 course);
void bingo_set_rando_star(enum CourseNum course, u8 starIndex);
u8 bingo_get_rando_star_status(enum CourseNum course, u8 starIndex);
u8 bingo_get_rando_star_count_course(enum CourseNum course);


#endif /* _BINGO_TRACKING_STAR_H */