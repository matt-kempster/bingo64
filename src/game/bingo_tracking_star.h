#ifndef _BINGO_TRACKING_STAR_H
#define _BINGO_TRACKING_STAR_H

#include <ultra64.h>
#include "area.h"

extern u32 gbCourseStars[25];

void bingo_tracking_star_reset(void);
void bingo_set_star(s16 course, s16 star);
s32 bingo_get_course_count(enum CourseNum course);
s32 bingo_get_star_count(void);
u8 bingo_get_course_flags(s16 course);
void bingo_set_rando_star(enum CourseNum course, u8 starIndex);
u8 bingo_get_rando_star_status(enum CourseNum course, u8 starIndex);
u8 bingo_get_rando_star_count_course(enum CourseNum course);


#endif /* _BINGO_TRACKING_STAR_H */