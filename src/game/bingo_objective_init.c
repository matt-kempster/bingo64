#include <ultra64.h>
#include "area.h"
#include "bingo.h"

#include "engine/behavior_script.h"
#include "engine/math_util.h"
#include "bingo_const.h"
#include "bingo_descriptions.h"
#include "bingo_objective_func.h"
#include "bingo_objective_init.h"
#include "bingo_objective_info.h"
#include "bingo_titles.h"
#include "strcpy.h"
#include "level_table.h"

s32 random_abc(enum CourseNum *course, s32 *star, char **hint) {
    u32 the_star = random_u16() % numPossibleABC;
    *course = possibleABC[the_star].course;
    *star = possibleABC[the_star].star - 1; // star is 0-indexed everywhere else :(
    *hint = possibleABC[the_star].hint;
}

enum CourseNum random_main_course() {
    return (random_u16() % 15) + 1;
}

enum CourseNum random_course_including_special() {
    return (random_u16() % 24) + 1;
}

s32 random_star_main_course(enum CourseNum *course, s32 *star) {
    *course = random_main_course();
    *star = (random_u16() % 7); // zero-indexed
}

s32 random_star_main_course_except_100c(enum CourseNum *course, s32 *star) {
    *course = random_main_course();
    *star = (random_u16() % 6); // zero-indexed
}

void random_star_except_mips_toad(enum CourseNum *course, s32 *star) {
    // This is gonna be a bit janky since I want every star to have equal prob.
    s32 starIndex = (random_u16() % 115); // 120 - (mips * 2) - (toad * 3)
    if (starIndex < (15 * 7)) {
        *course = (starIndex % 15) + 1;
        *star = starIndex % 7;
        return;
    }

    *star = 0;

    switch (starIndex - (15 * 7)) {
        case 0:
        case 1:
            // PSS
            *course = COURSE_PSS;
            *star = starIndex - (15 * 7); // PSS_STAR_SLOW or PSS_STAR_FAST
            break;
        case 2:
            // SA
            *course = COURSE_SA;
            break;
        case 3:
            // TotWC
            *course = COURSE_TOTWC;
            break;
        case 4:
            // CotMC
            *course = COURSE_COTMC;
            break;
        case 5:
            // VCutM
            *course = COURSE_VCUTM;
            break;
        case 6:
            // WMotR
            *course = COURSE_WMOTR;
            break;
        case 7:
            // BitDW
            *course = COURSE_BITDW;
            break;
        case 8:
            // BitFS
            *course = COURSE_BITFS;
            break;
        case 9:
            // BitS
            *course = COURSE_BITS;
            break;
    }

    return;
}

void random_bbc(enum CourseNum *course, s32 *star) {
retry:
    random_star_except_mips_toad(course, star);
    if (*course == COURSE_BOB && *star == 0) {
        // King Bob-omb on the summit can't be collected because you
        // have to pick him up
        goto retry;
    }
    if (*course == COURSE_TTM && *star == 1) {
        // Mystery of the Monkey Cage can't be collected because you
        // have to pick Ukiki up
        goto retry;
    }
    if (*course == COURSE_CCM && *star == 1) {
        // Li'l Penguin Lost is possible, but it's very hard. You have
        // to lead the lil penguin down to the bottom platform which requires
        // many hard techniques
        goto retry;
    }
}

void random_zbc(enum CourseNum *course, s32 *star) {
retry:
    random_star_except_mips_toad(course, star);
    if (*course == COURSE_WF && *star == 0) {
        // chip off whomps block is impossible, have to ground pound
        goto retry;
    }
    if (*course == COURSE_JRB && *star == 6) {
        // 100c in JRB is impossible because only 104 coins and need to
        // ground pound the blue coin thing
        goto retry;
    }
    // should i outlaw chain chomps gate in BoB because it requires bomb clip?
    if (*course == COURSE_DDD && *star == 6) {
        // not that many coins, and have to use difficult shell cloning
        goto retry;
    }
    // should I outlaw SL (igloo stuff) because you have to clip through it?
    // should I outlaw THI wiggler cave stuff because you have to clip using Chuckya?
    if (*course == COURSE_TTC && *star == 6) {
        // have to clone coins until you get 100 without blue coins
        goto retry;
    }
    // RELEVANT FOR OTHER GAME MODES:
    // vcutm can't be accessed without ground pounding the pillars
    // although ofc you can use PU speed lol
}


s32 random_range_inclusive(s32 low, s32 high) {
    return low + (random_u16() % (high - low + 1));
}

////////////////////////////////////////////////////////////////////////////////

s32 bingo_objective_star_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 star;

    switch (class) {
        default:
            random_star_except_mips_toad(&course, &star);
            break;
    }
    objective->data.starObjective.course = course;
    objective->data.starObjective.starIndex = star;
}

s32 bingo_objective_star_a_button_challenge_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 star;
    char *hint;

    switch (class) {
        default:
            random_abc(&course, &star, &hint);
            break;
    }
    objective->data.abcStarObjective.course = course;
    objective->data.abcStarObjective.starIndex = star;
    objective->data.abcStarObjective.hint = hint;
}

s32 bingo_objective_star_b_button_challenge_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 star;

    switch (class) {
        default:
            random_bbc(&course, &star);
            break;
    }
    objective->data.starObjective.course = course;
    objective->data.starObjective.starIndex = star;
}

s32 bingo_objective_star_z_button_challenge_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 star;

    switch (class) {
        default:
            random_zbc(&course, &star);
            break;
    }

    objective->data.starObjective.course = course;
    objective->data.starObjective.starIndex = star;
}

s32 bingo_objective_star_timed_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 star, maxTime;

    switch (class) {
        default:
            random_star_main_course_except_100c(&course, &star);
            maxTime = 30 * (  // convert frames to seconds
                get_time_for_star(course, star)
                + 20  // add time to be nice
                + random_range_inclusive(0, 15)  // random extra seconds
            );
            break;
        case BINGO_CLASS_HARD:
        case BINGO_CLASS_CENTER:
            random_star_main_course_except_100c(&course, &star);
            maxTime = 30 * (
                get_time_for_star(course, star)
                + random_range_inclusive(0, 15)  // random extra seconds
            );
            break;
    }

    objective->data.starTimerObjective.course = course;
    objective->data.starTimerObjective.starIndex = star;
    objective->data.starTimerObjective.timer = 0;
    objective->data.starTimerObjective.maxTime = maxTime;
}

s32 bingo_objective_star_ttc_random_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    s32 star;
    switch (class) {
        default:
            star = random_u16() % 7;
            break;
    }

    objective->data.starObjective.course = COURSE_TTC;
    objective->data.starObjective.starIndex = star;
}

s32 bingo_objective_star_reverse_joystick_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 star;

    switch (class) {
        default:
            random_star_except_mips_toad(&course, &star);
            break;
    }

    objective->data.starObjective.course = course;
    objective->data.starObjective.starIndex = star;
}

s32 bingo_objective_star_green_demon_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 star;

    switch (class) {
        default:
            random_star_main_course_except_100c(&course, &star);
            break;
    }

    objective->data.starObjective.course = course;
    objective->data.starObjective.starIndex = star;
}

s32 bingo_objective_star_click_game_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 star;
    s32 clicks, clickDiff;
    struct ClickInfo *clickInfo;

    do {
        random_star_main_course(&course, &star);
        clickInfo = get_click_info_for_star(course, star);
    } while (clickInfo->minClicks == CLICK_GAME_STAR_BANNED);

    if (
        clickInfo->maxClicks == CLICK_GAME_MAX_IS_MIN
        || clickInfo->maxClicks == clickInfo->minClicks
    ) {
        clicks = clickInfo->minClicks;
    } else {
        switch (class) {
            default:
                clickDiff = clickInfo->maxClicks - clickInfo->minClicks;
                clicks = clickInfo->minClicks + (random_u16() % clickDiff) + 1;
                break;
            case BINGO_CLASS_HARD:
                clicks = clickInfo->minClicks;
                break;
        }
    }

    objective->data.starClicksObjective.course = course;
    objective->data.starClicksObjective.starIndex = star;
    objective->data.starClicksObjective.maxClicks = clicks;
    objective->data.starClicksObjective.clicks = -1;  // a bug adds a click on level start
}

s32 bingo_objective_star_daredevil_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 star;

    // Some thoughts on Daredevil:
    //   - All stars are not much harder, except for...
    //   - DDD: It might be possible to get to the sub area,
    //          but I haven't managed to do it. Close, though.
    //          The Manta Ray heals you, so it's possible.
    //          Coins make Chests in the Current possible.
    //   - WDW: Getting to downtown seems impossible without
    //          glitches. This forbids 8 red coins and Quick Race.
    //          100 coins is possible (and fun).
    //   - JRB: Using the metal cap it's actually possible,
    //          though difficult, to get to the ocean cove.
    //          However, I haven't managed to get from the
    //          cove back to the surface (though close), which
    //          means only Treasure of the Ocean Cove, Blast to
    //          the Stone Pillar, and Through the Jet Stream seem
    //          possible. Can the Eel Come Out to Play might be
    //          possible - haven't tried yet - but I bet it'd be
    //          hard. Same with 100 coins. To be determined.
    switch (class) {
        default:
        case BINGO_CLASS_MEDIUM:
            do {
                random_star_except_mips_toad(&course, &star);
            } while (
                (
                    course == COURSE_JRB
                    || (course == COURSE_DDD && (star != 1 || star != 4))
                    || (course == COURSE_WDW && (star == 4 || star == 5))
                    // COURSE_SA is actually possible/not hard
                )
            );
            break;
        case BINGO_CLASS_HARD:
            do {
                course = random_main_course();
            } while (course == COURSE_DDD);
            if (course == COURSE_JRB) {
                switch (random_u16() % 3) {
                    case 0:
                        star = 2;
                    case 1:
                        star = 4;
                    case 2:
                        star = 5;
                }
            } else {
                star = 6;  // 100 coins
            }
            break;
    }

    objective->data.starObjective.course = course;
    objective->data.starObjective.starIndex = star;
}

s32 bingo_objective_star_random_reds_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 star;

    switch (class) {
        default:
        case BINGO_CLASS_EASY:
        case BINGO_CLASS_MEDIUM:
        case BINGO_CLASS_CENTER:
        case BINGO_CLASS_HARD:
            // PSS is the only course without red coins
            do {
                course = random_course_including_special();
            } while (course == COURSE_PSS);
    }

    switch (course) {
        default:
            star = 0;
            break;
        case COURSE_BOB:
        case COURSE_WF:
        case COURSE_JRB:
        case COURSE_CCM:
        case COURSE_BBH:
            star = 3;
            break;
        case COURSE_HMC:
            star = 1;
            break;
        case COURSE_LLL:
        case COURSE_DDD:
        case COURSE_TTM:
        case COURSE_RR:
            star = 2;
            break;
        case COURSE_SSL:
        case COURSE_SL:
        case COURSE_WDW:
        case COURSE_THI:
            star = 4;
            break;
        case COURSE_TTC:
            star = 5;
            break;
    }

    objective->data.starObjective.course = course;
    objective->data.starObjective.starIndex = star;
}

void random_main_course_coins(enum BingoObjectiveClass class, enum CourseNum *course, s32 *coins) {
    switch (class) {
        default:
        case BINGO_CLASS_EASY:
            *course = random_main_course();
            *coins = random_range_inclusive(30, 60);
            break;
        case BINGO_CLASS_MEDIUM:
            *course = random_main_course();
            *coins = random_range_inclusive(50, 99);
            break;
        case BINGO_CLASS_CENTER:
        case BINGO_CLASS_HARD:
            *course = random_main_course();
            *coins = random_range_inclusive(80, 99);
            break;
    }
}

void random_special_course_coins(enum BingoObjectiveClass class, enum CourseNum *course, s32 *coins) {
    f32 max;
    f32 min;
    switch (class) {
        case BINGO_CLASS_EASY:
            min = 0.4f;
            max = 0.5f;
            break;
        case BINGO_CLASS_MEDIUM:
            min = 0.5f;
            max = 0.6f;
            break;
        default:
        case BINGO_CLASS_HARD:
            min = 0.6f;
            max = 1.0f;
            break;
    }

    *course = random_range_inclusive(COURSE_BITDW, COURSE_SA);
    switch (*course) {
        case COURSE_BITDW:
            *coins = random_range_inclusive((s32)(80 * min), (s32)(80 * max));
            break;
        case COURSE_BITFS:
            *coins = random_range_inclusive((s32)(80 * min), (s32)(80 * max));
            break;
        case COURSE_BITS:
            *coins = random_range_inclusive((s32)(76 * min), (s32)(76 * max));
            break;
        case COURSE_PSS:
            *coins = random_range_inclusive((s32)(80 * min), (s32)(80 * max));
            break;
        case COURSE_COTMC:
            // There's only 47 coins, which isn't too hard.
            *coins = random_range_inclusive(37, 47);
            break;
        case COURSE_TOTWC:
            // There are actually 63 coins but it's far too hard to
            // get that many.
            *coins = random_range_inclusive(37, 53);
            break;
        case COURSE_VCUTM:
            // There aren't enough coins to have a lower goal.
            *coins = 27;
            break;
        case COURSE_WMOTR:
            *coins = random_range_inclusive((s32)(56 * min), (s32)(56 * max));
            break;
        case COURSE_SA:
            // There's nothing difficult about getting all the coins
            // so just have that be the default.
            *coins = 56;
            break;
    }
}


s32 bingo_objective_coin_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 coins;

    // For some reason, I want the main courses to have
    // this stake in these coin objectives. It feels "right".
    if (random_range_inclusive(1, 100) <= 76) {
        random_main_course_coins(class, &course, &coins);
    } else {
        random_special_course_coins(class, &course, &coins);
    }

    objective->data.courseCollectableData.course = course;
    objective->data.courseCollectableData.toGet = coins;
    objective->data.courseCollectableData.gotten = 0;
}

s32 bingo_objective_splatoon_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course = random_main_course();
    s32 pct;
    s32 target;

    switch (class) {
        case BINGO_CLASS_EASY:
            pct = random_range_inclusive(10, 20);
            break;
        case BINGO_CLASS_HARD:
            pct = random_range_inclusive(40, 55);
            break;
        case BINGO_CLASS_MEDIUM:
        default:
            pct = random_range_inclusive(25, 35);
            break;
    }

    // Round to tens so the target reads cleanly on the board.
    target = (get_floors_in_course(course) * pct / 100) / 10 * 10;
    if (target < 20) {
        target = 20;
    }

    objective->data.courseCollectableData.course = course;
    objective->data.courseCollectableData.toGet = target;
    objective->data.courseCollectableData.gotten = 0;
}

s32 bingo_objective_1ups_in_level_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;
    s32 _1ups;
    s32 _1upsMin;
    switch (class) {
        case BINGO_CLASS_MEDIUM:
        default:
            do {
                course = random_course_including_special();
                _1upsMin = (s32) get_1ups_in_level(course) * 0.6f;
                if (get_1ups_in_level(course) == _1upsMin) {
                    _1ups = _1upsMin;
                    random_u16();
                } else {
                    _1ups = (random_u16() % (get_1ups_in_level(course) - _1upsMin)) + _1upsMin;
                }
            } while (_1ups < 3);
            break;
        case BINGO_CLASS_HARD:
            do {
                course = random_main_course();
                _1ups = get_1ups_in_level(course);
            } while (_1ups < 2);  // allow 2, but not 1 or 0
            break;
    }

    objective->data.courseCollectableData.course = course;
    objective->data.courseCollectableData.toGet = _1ups;
    objective->data.courseCollectableData.gotten = 0;
}

s32 bingo_objective_stars_in_level_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum CourseNum course;

    switch (class) {
        default:
            course = random_main_course();
            break;
    }

    objective->data.courseCollectableData.course = course;
    objective->data.courseCollectableData.toGet = 7;
    objective->data.courseCollectableData.gotten = 0;
}

s32 bingo_objective_dangerous_wall_kicks_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    s32 toGetTotal;
    s32 toGetEachCourse;

    switch (class) {
        default:
            toGetTotal = random_range_inclusive(2, 4);
            toGetEachCourse = random_range_inclusive(5, 7);
            break;
    }

    objective->data.multiCourseCollectableData.toGetTotal = toGetTotal;
    objective->data.multiCourseCollectableData.gottenTotal = 0;
    objective->data.multiCourseCollectableData.toGetEachCourse = toGetEachCourse;
    objective->data.multiCourseCollectableData.gottenThisCourse = 0;
}

s32 bingo_objective_racing_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    objective->data.collectableData.toGet = 3;
    objective->data.collectableData.gotten = 0;
}

s32 bingo_objective_secrets_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    objective->data.collectableData.toGet = 4;
    objective->data.collectableData.gotten = 0;
}

s32 bingo_objective_bowser_init(
    struct BingoObjective *objective, enum BingoObjectiveClass class
) {
    enum LevelNum level;

    switch (class) {
        default:
            if (random_u16() % 2 == 0) {
                level = LEVEL_BOWSER_1;
            } else {
                level = LEVEL_BOWSER_2;
            }
            break;
        case BINGO_CLASS_CENTER:
        case BINGO_CLASS_HARD:
            level = LEVEL_BOWSER_3;
            break;
    }

    objective->data.levelData.level = level;
}

s32 bingo_objective_roof_without_cannon_init(
    UNUSED struct BingoObjective *objective, UNUSED enum BingoObjectiveClass class
) {
    return 0;
}

s32 bingo_objective_lose_mario_hat_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(3, 4);
    }
}

s32 bingo_objective_unique_deaths_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(4, 6);
    }
}

s32 bingo_objective_blj_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(3, 6);
    }
}

s32 bingo_objective_multicoin_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(200, 350);
    }
}

s32 bingo_objective_multistar_init(enum BingoObjectiveClass class) {
    switch (class) {
        case BINGO_CLASS_EASY:
            return random_range_inclusive(3, 4);
        case BINGO_CLASS_MEDIUM:
            return random_range_inclusive(5, 8);
        default:
            return random_range_inclusive(8, 12);
    }
}

s32 bingo_objective_stars_multiple_levels(enum BingoObjectiveClass class) {
    switch (class) {
        case BINGO_CLASS_EASY:
            return random_range_inclusive(3, 5);
        case BINGO_CLASS_MEDIUM:
            return random_range_inclusive(4, 6);
        case BINGO_CLASS_HARD:
            return random_range_inclusive(7, 10);
        case BINGO_CLASS_CENTER:
            return random_range_inclusive(11, 15);
    }
}

s32 bingo_objective_exclamation_mark_box_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(6, 14);
    }
}

s32 bingo_objective_wing_cap_box_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(5, 9);
    }
}

s32 bingo_objective_vanish_cap_box_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(4, 7);
    }
}

s32 bingo_objective_metal_cap_box_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(5, 9);
    }
}

s32 bingo_objective_signpost_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(7, 13);
        case BINGO_CLASS_HARD:
            return random_range_inclusive(14, 20);
    }
}

s32 bingo_objective_poles_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(10, 17);
        case BINGO_CLASS_CENTER:
            // 27 was chosen here because that would mean getting every
            // pole in LLL, WMotR, RR, and DDD (if you were to go to the
            // minimum number of levels). That's nearly too tough.
            return random_range_inclusive(20, 27);
    }
}

s32 bingo_objective_cannons_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(6, 10);
        case BINGO_CLASS_CENTER:
            return random_range_inclusive(11, 20);
    }
}

s32 bingo_objective_red_coin_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(12, 18);
        case BINGO_CLASS_HARD:
            return random_range_inclusive(20, 29);
    }
}

s32 bingo_objective_amps_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(5, 10);
        case BINGO_CLASS_HARD:
        case BINGO_CLASS_CENTER:
            return random_range_inclusive(11, 16);
    }
}

s32 bingo_objective_kill_goombas_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(5, 14);
        case BINGO_CLASS_CENTER:
            return random_range_inclusive(15, 19);
    }
}

s32 bingo_objective_kill_bobombs_init(enum BingoObjectiveClass class) {
    switch (class) {
        case BINGO_CLASS_CENTER:
            return random_range_inclusive(15, 19);
        default:
            return random_range_inclusive(5, 14);
    }
}

s32 bingo_objective_kill_spindrifts_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(9, 16);
    }
}

s32 bingo_objective_kill_mr_is_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(3, 6);
    }
}

s32 bingo_objective_kill_scuttlebugs_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(4, 9);
    }
}

s32 bingo_objective_kill_bullies_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(6, 13);
        case BINGO_CLASS_HARD:
            return random_range_inclusive(11, 16);
    }
}

s32 bingo_objective_kill_chuckyas_init(enum BingoObjectiveClass class) {
    switch (class) {
        default:
            return random_range_inclusive(2, 3);
        case BINGO_CLASS_HARD:
            return random_range_inclusive(3, 5);
    }
}

s32 bingo_objective_collectable_init(struct BingoObjective *obj, s32 toGet) {
    obj->data.collectableData.toGet = toGet;
    obj->data.collectableData.gotten = 0;
}

////////////////////////////////////////////////////////////////////////////////

s32 bingo_objective_collectable_init_dispatch(
    enum BingoObjectiveClass class, enum BingoObjectiveType type
) {
    switch (type) {
        case BINGO_OBJECTIVE_MULTICOIN:
            return bingo_objective_multicoin_init(class);
        case BINGO_OBJECTIVE_MULTISTAR:
            return bingo_objective_multistar_init(class);
        case BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS:
            return bingo_objective_stars_multiple_levels(class);
        case BINGO_OBJECTIVE_BLJ:
            return bingo_objective_blj_init(class);
        case BINGO_OBJECTIVE_LOSE_MARIO_HAT:
            return bingo_objective_lose_mario_hat_init(class);
        case BINGO_OBJECTIVE_UNIQUE_DEATHS:
            return bingo_objective_unique_deaths_init(class);
        case BINGO_OBJECTIVE_EXCLAMATION_MARK_BOX:
            return bingo_objective_exclamation_mark_box_init(class);
        case BINGO_OBJECTIVE_WING_CAP_BOX:
            return bingo_objective_wing_cap_box_init(class);
        case BINGO_OBJECTIVE_VANISH_CAP_BOX:
            return bingo_objective_vanish_cap_box_init(class);
        case BINGO_OBJECTIVE_METAL_CAP_BOX:
            return bingo_objective_metal_cap_box_init(class);
        case BINGO_OBJECTIVE_SIGNPOST:
            return bingo_objective_signpost_init(class);
        case BINGO_OBJECTIVE_POLES:
            return bingo_objective_poles_init(class);
        case BINGO_OBJECTIVE_SHOOT_CANNONS:
            return bingo_objective_cannons_init(class);
        case BINGO_OBJECTIVE_RED_COIN:
            return bingo_objective_red_coin_init(class);
        case BINGO_OBJECTIVE_AMPS:
            return bingo_objective_amps_init(class);
        case BINGO_OBJECTIVE_KILL_GOOMBAS:
            return bingo_objective_kill_goombas_init(class);
        case BINGO_OBJECTIVE_KILL_BOBOMBS:
            return bingo_objective_kill_bobombs_init(class);
        case BINGO_OBJECTIVE_KILL_SPINDRIFTS:
            return bingo_objective_kill_spindrifts_init(class);
        case BINGO_OBJECTIVE_KILL_MR_IS:
            return bingo_objective_kill_mr_is_init(class);
        case BINGO_OBJECTIVE_KILL_SCUTTLEBUGS:
            return bingo_objective_kill_scuttlebugs_init(class);
        case BINGO_OBJECTIVE_KILL_BULLIES:
            return bingo_objective_kill_bullies_init(class);
        case BINGO_OBJECTIVE_KILL_CHUCKYAS:
            return bingo_objective_kill_chuckyas_init(class);
    }
}

s32 bingo_objective_init_dispatch(
    struct BingoObjective *objective,
    enum BingoObjectiveClass class,
    enum BingoObjectiveType type
) {
    s32 collectables;
    switch (type) {
        case BINGO_OBJECTIVE_STAR:
            return bingo_objective_star_init(objective, class);
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
            return bingo_objective_star_a_button_challenge_init(objective, class);
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
            return bingo_objective_star_b_button_challenge_init(objective, class);
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
            return bingo_objective_star_z_button_challenge_init(objective, class);
        case BINGO_OBJECTIVE_STAR_TIMED:
            return bingo_objective_star_timed_init(objective, class);
        case BINGO_OBJECTIVE_STAR_TTC_RANDOM:
            return bingo_objective_star_ttc_random_init(objective, class);
        case BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK:
            return bingo_objective_star_reverse_joystick_init(objective, class);
        case BINGO_OBJECTIVE_STAR_GREEN_DEMON:
            return bingo_objective_star_green_demon_init(objective, class);
        case BINGO_OBJECTIVE_STAR_CLICK_GAME:
            return bingo_objective_star_click_game_init(objective, class);
        case BINGO_OBJECTIVE_STAR_DAREDEVIL:
            return bingo_objective_star_daredevil_init(objective, class);
        case BINGO_OBJECTIVE_RANDOM_RED_COINS:
            return bingo_objective_star_random_reds_init(objective, class);
        case BINGO_OBJECTIVE_COIN:
            return bingo_objective_coin_init(objective, class);
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
            return bingo_objective_1ups_in_level_init(objective, class);
        case BINGO_OBJECTIVE_SPLATOON:
            return bingo_objective_splatoon_init(objective, class);
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
            return bingo_objective_stars_in_level_init(objective, class);
        case BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS:
            return bingo_objective_dangerous_wall_kicks_init(objective, class);
        case BINGO_OBJECTIVE_RACING_STARS:
            return bingo_objective_racing_init(objective, class);
        case BINGO_OBJECTIVE_SECRETS_STARS:
            return bingo_objective_secrets_init(objective, class);
        case BINGO_OBJECTIVE_BOWSER:
            return bingo_objective_bowser_init(objective, class);
        case BINGO_OBJECTIVE_ROOF_WITHOUT_CANNON:
            return bingo_objective_roof_without_cannon_init(objective, class);
    }
    if (BINGO_OBJECTIVE_COLLECTABLE_MIN <= type && type <= BINGO_OBJECTIVE_COLLECTABLE_MAX) {
        collectables = bingo_objective_collectable_init_dispatch(class, type);
        return bingo_objective_collectable_init(objective, collectables);
    }
}

s32 bingo_objective_init(
    struct BingoObjective *objective,
    enum BingoObjectiveClass class,
    enum BingoObjectiveType type
) {
    objective->initialized = 1;
    // A previous session's board lives in the same global array; without
    // this, a cell completed last race stays COMPLETE on the new board.
    objective->state = BINGO_STATE_NONE;
    objective->type = type;
    objective->class = class;
    objective->icon = get_objective_info(type)->icon;
    bingo_objective_init_dispatch(objective, class, type);
    get_objective_title(objective);
}