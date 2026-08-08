#include <ultra64.h>
#include <PR/os_libc.h>

#include "area.h"
#include "bingo.h"

#include "bingo_descriptions.h"
#include "bingo_const.h"
#include "splatoon.h"
#include "bingo_objective_func.h"
#include "bingo_objective_init.h"
#include "bingo_tracking_star.h"
#include "segment2.h"
#include "strcpy.h"
#include "camera.h"
#include "ingame_menu.h"
#include "print.h"
#include "hud.h"
#include "bingo_tracking_collectables.h"
#include "room.h"
#include "object_constants.h"


s32 objective_obtain_star(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    s32 course;
    s32 star;

    if (update == BINGO_UPDATE_STAR) {
        course = objective->data.starObjective.course;
        star = objective->data.starObjective.starIndex;
        if (gCurrCourseNum == course && gbStarIndex == star) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        }
    }
}

s32 objective_obtain_abz_button_challenges(
    struct BingoObjective *objective,
    enum BingoObjectiveUpdate update,
    enum BingoObjectiveUpdate buttonUpdate
) {
    if (update == BINGO_UPDATE_COURSE_CHANGED) {
        objective->state = BINGO_STATE_NONE;
    } else if (
        update == buttonUpdate
        && gCurrCourseNum == objective->data.courseCollectableData.course
    ) {
        set_objective_state(objective, BINGO_STATE_FAILED_IN_THIS_COURSE);
    } else if (objective->state != BINGO_STATE_FAILED_IN_THIS_COURSE) {
        objective_obtain_star(objective, update);
    }
}

s32 objective_obtain_star_random_reds(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    if (gBingoStarSelected != BINGO_MODIFIER_ORDERED_RED_COINS) {
        return;
    }
    if (update == BINGO_UPDATE_COURSE_CHANGED) {
        objective->state = BINGO_STATE_NONE;
    } else if (
        update == BINGO_UPDATE_WRONG_RED_COIN
        && gCurrCourseNum == objective->data.courseCollectableData.course
    ) {
        set_objective_state(objective, BINGO_STATE_FAILED_IN_THIS_COURSE);
    } else if (objective->state != BINGO_STATE_FAILED_IN_THIS_COURSE) {
        objective_obtain_star(objective, update);
    }
}

s32 objective_obtain_star_timer(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    struct StarTimerObjectiveData *data = &objective->data.starTimerObjective;

    if (update == BINGO_UPDATE_COURSE_CHANGED) {
        objective->state = BINGO_STATE_NONE;
        data->timer = 0;
    } else if (objective->state == BINGO_STATE_FAILED_IN_THIS_COURSE) {
        return;
    } else if (
        update == BINGO_UPDATE_TIMER_FRAME_STAR
        && gCurrCourseNum == data->course
        && !gbBingoTimerDisabled
    ) {
        data->timer++;
        if (data->timer > data->maxTime) {
            set_objective_state(objective, BINGO_STATE_FAILED_IN_THIS_COURSE);
        }
    } else if (objective->state != BINGO_STATE_FAILED_IN_THIS_COURSE) {
        objective_obtain_star(objective, update);
    }
}

s32 objective_obtain_ttc_random_star(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    if (gTTCSpeedSetting == TTC_SPEED_RANDOM) {
        objective_obtain_star(objective, update);
    }
}

s32 objective_obtain_star_reverse_joystick(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    if (gBingoReverseJoystickActive) {
        objective_obtain_star(objective, update);
    }
}

s32 objective_obtain_star_greendemon(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    if (gBingoStarSelected == BINGO_MODIFIER_GREEN_DEMON) {
        objective_obtain_star(objective, update);
    }
}


s32 objective_obtain_star_click_game(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    // This function will probably be combined with the A button challenge
    // once ABC with multiple A presses is supported
    struct StarClickCounterData *data = &objective->data.starClicksObjective;

    if (update == BINGO_UPDATE_COURSE_CHANGED) {
        objective->state = BINGO_STATE_NONE;
        data->clicks = -1;  // to fix bug where entering a level is a click
    } else if (objective->state == BINGO_STATE_FAILED_IN_THIS_COURSE) {
        return;
    } else if (update == BINGO_UPDATE_CAMERA_CLICK && gCurrCourseNum == data->course) {
        data->clicks++;
        if (data->clicks > data->maxClicks) {
            set_objective_state(objective, BINGO_STATE_FAILED_IN_THIS_COURSE);
        }
    } else if (gBingoClickGameActive) {
        objective_obtain_star(objective, update);
    }
}

s32 objective_obtain_star_daredevil(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    if (gBingoDaredevilActive) {
        objective_obtain_star(objective, update);
    }
}

s32 objective_obtain_coins(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    struct CourseCollectableData *data = &objective->data.courseCollectableData;

    if (update == BINGO_UPDATE_COURSE_CHANGED) {
        objective->state = BINGO_STATE_NONE;
        data->gotten = 0;
    } else if (update == BINGO_UPDATE_COIN && gCurrCourseNum == data->course) {
        data->gotten += gbCoinsJustGotten;
        if (data->gotten >= data->toGet) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        }
    }
}

s32 objective_splatoon(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    struct CourseCollectableData *data = &objective->data.courseCollectableData;

    if (update == BINGO_UPDATE_COURSE_CHANGED) {
        objective->state = BINGO_STATE_NONE;
        data->gotten = 0;
    } else if (update == BINGO_UPDATE_SPLATOON_PAINTED && gCurrCourseNum == data->course) {
        data->gotten = gSplatoonPaintedCount;
        if (data->gotten >= data->toGet) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        }
    }
}

s32 objective_obtain_multicoin(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    struct CollectableData *data = &objective->data.collectableData;

    if (update == BINGO_UPDATE_COIN) {
        data->gotten += gbCoinsJustGotten;
        if (data->gotten >= data->toGet) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        }
    }
}

s32 objective_obtain_multistar(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    struct CollectableData *data = &objective->data.collectableData;

    if (update == BINGO_UPDATE_STAR) {
        data->gotten = bingo_get_star_count();
        if (data->gotten >= data->toGet) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        }
    }
}

s32 objective_obtain_1ups_in_level(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    struct CourseCollectableData *data = &objective->data.courseCollectableData;

    if (update == BINGO_UPDATE_COURSE_CHANGED) {
        objective->state = BINGO_STATE_NONE;
        data->gotten = 0;
    } else if (update == BINGO_UPDATE_GOT_1UP && gCurrCourseNum == data->course) {
        data->gotten++;
        bingo_hud_update_number(objective->icon, data->gotten);
        if (data->gotten == data->toGet) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        }
    }
}

s32 objective_obtain_stars_in_level(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    enum CourseNum course = objective->data.courseCollectableData.course;
    if (update == BINGO_UPDATE_STAR && gCurrCourseNum == course) {
        if (bingo_get_course_count(course) >= 7) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        }
    }
}

s32 objective_racing_stars(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    s32 count = 0;
    s32 old_count = 0;
    if (update == BINGO_UPDATE_STAR) {
        if (bingo_get_course_flags(COURSE_BOB - 1) & (1 << (2 - 1))) {
            // Footrace with Koopa the Quick
            count++;
        }
        if (bingo_get_course_flags(COURSE_CCM - 1) & (1 << (3 - 1))) {
            // Big Penguin Race
            count++;
        }
        if (bingo_get_course_flags(COURSE_THI - 1) & (1 << (3 - 1))) {
            // Rematch with Koopa the Quick
            count++;
        }
        old_count = objective->data.collectableData.gotten;
        objective->data.collectableData.gotten = count;
        if (count == 3) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        } else if (count > old_count) {
            bingo_hud_update_number(objective->icon, count);
        }
    }
}

s32 objective_secrets_stars(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    s32 count = 0;
    s32 old_count = 0;
    if (update == BINGO_UPDATE_STAR) {
        if (bingo_get_course_flags(COURSE_BOB - 1) & (1 << (5 - 1))) {
            // Mario Wings to the Sky
            count++;
        }
        if (bingo_get_course_flags(COURSE_SSL - 1) & (1 << (6 - 1))) {
            // Pyramid Puzzle
            count++;
        }
        if (bingo_get_course_flags(COURSE_WDW - 1) & (1 << (3 - 1))) {
            // Secrets In the Shallows & Sky
            count++;
        }
        if (bingo_get_course_flags(COURSE_THI - 1) & (1 << (4 - 1))) {
            // Five Itty Bitty Secrets
            count++;
        }
        old_count = objective->data.collectableData.gotten;
        objective->data.collectableData.gotten = count;
        if (count == 4) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        } else if (count > old_count) {
            bingo_hud_update_number(objective->icon, count);
        }
    }
}

s32 objective_stars_multiple_levels(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    s32 count = 0;
    s32 old_count = 0;
    enum CourseNum course;
    if (update == BINGO_UPDATE_STAR) {
        for (course = COURSE_BOB; course < COURSE_RR; course++) {
            count += bingo_get_course_count(course - 1) > 0 ? 1 : 0;
        }
        old_count = objective->data.collectableData.gotten;
        objective->data.collectableData.gotten = count;
        if (count == objective->data.collectableData.toGet) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        } else if (count > old_count) {
            bingo_hud_update_number(objective->icon, count);
        }
    }
}

s32 objective_lose_mario_hat(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    struct CollectableFlagsData *data = &objective->data.collectableFlagsData;
    u32 flags, flag;
    s32 count = 0;

    flags = data->flags;
    while (flags) {
        count += flags & 1;
        flags >>= 1;
    }
    if (BINGO_UPDATE_LOST_HAT_FLAGS_BEGIN <= update && update <= BINGO_UPDATE_LOST_HAT_FLAGS_END) {
        flag = 1 << (update - BINGO_UPDATE_LOST_HAT_FLAGS_BEGIN);
        if (!(data->flags & flag)) {
            data->flags |= flag;
            count++;
            if (count >= data->toGet) {
                set_objective_state(objective, BINGO_STATE_COMPLETE);
            } else {
                bingo_hud_update_number(objective->icon, count);
            }
        }
    }
}

s32 objective_blj(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    struct CollectableData *data = &objective->data.collectableData;
    s32 objIndex = objective - gBingoObjectives;  // to uniquely identify _this_ objective
    u32 uid;

    if (update == BINGO_UPDATE_BLJ) {
        uid = get_unique_id(BINGO_UPDATE_BLJ, objIndex, 0.0f, 0.0f);
        if (!is_new_kill(BINGO_UPDATE_BLJ, uid)) {
            return;
        }
        data->gotten++;
        bingo_hud_update_number(objective->icon, data->gotten);
        if (data->gotten >= data->toGet) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        }
    }
}

s32 objective_bowser(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    if (
        update == BINGO_UPDATE_BOWSER_KILLED
        && gCurrLevelNum == objective->data.levelData.level
    ) {
        set_objective_state(objective, BINGO_STATE_COMPLETE);
    }
}

s32 objective_roof_without_cannon(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    if (update == BINGO_UPDATE_COURSE_CHANGED) {
        objective->state = BINGO_STATE_NONE;
    } else if (
        update == BINGO_UPDATE_SHOT_FROM_CANNON
        && gCurrLevelNum == LEVEL_CASTLE_GROUNDS
    ) {
        set_objective_state(objective, BINGO_STATE_FAILED_IN_THIS_COURSE);
    } else if (
        objective->state != BINGO_STATE_FAILED_IN_THIS_COURSE
        && update == BINGO_UPDATE_ON_CASTLE_ROOF
    ) {
        set_objective_state(objective, BINGO_STATE_COMPLETE);
    }
}

s32 objective_generic_collectable(
    struct BingoObjective *objective,
    enum BingoObjectiveUpdate update,
    enum BingoObjectiveUpdate desiredUpdate
) {
    struct CollectableData *data = &objective->data.collectableData;
    if (update == desiredUpdate) {
        data->gotten++;
        bingo_hud_update_number(objective->icon, data->gotten);
        if (data->gotten >= data->toGet) {
            set_objective_state(objective, BINGO_STATE_COMPLETE);
        }
    }
}

s32 objective_dangerous_wall_kicks(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    struct MultiCourseCollectableData *data = &objective->data.multiCourseCollectableData;
    u32 uid;
    s32 objIndex = objective - gBingoObjectives;  // to uniquely identify _this_ objective
    char message[10];

    if (update == BINGO_UPDATE_DANGEROUS_WALL_KICK_FAILED) {
        if (data->gottenThisCourse != 0 && data->gottenThisCourse < data->toGetEachCourse) {
            bingo_hud_update_state(BINGO_ICON_FAILED, BINGO_ICON_DANGEROUS_WALL_KICKS);
        }
        data->gottenThisCourse = 0;
    } else if (update == BINGO_UPDATE_DANGEROUS_WALL_KICK) {
        uid = get_unique_id(BINGO_UPDATE_DANGEROUS_WALL_KICK, objIndex, 0.0f, 0.0f);
        if (!peek_would_be_new_kill(BINGO_UPDATE_DANGEROUS_WALL_KICK, uid)) {
            return;
        }
        data->gottenThisCourse++;
        bingo_hud_update_number(objective->icon, data->gottenThisCourse);
        if (data->gottenThisCourse >= data->toGetEachCourse) {
            data->gottenThisCourse = 0;
            data->gottenTotal++;
            is_new_kill(BINGO_UPDATE_DANGEROUS_WALL_KICK, uid);  // return value useless, just writing down.
            if (data->gottenTotal >= data->toGetTotal) {
                set_objective_state(objective, BINGO_STATE_COMPLETE);
            } else {
                sprintf(message, "%d COURSE%s", data->gottenTotal, data->gottenTotal == 1 ? "" : "S");
                bingo_hud_update_message(objective->icon, message, 1);
            }
        }
    }
}

s32 update_objective(struct BingoObjective *objective, enum BingoObjectiveUpdate update) {
    if (objective->state == BINGO_STATE_COMPLETE) {
        return;
    }

    switch (objective->type) {
        case BINGO_OBJECTIVE_STAR:
            return objective_obtain_star(objective, update);
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
            return objective_obtain_abz_button_challenges(objective, update, BINGO_UPDATE_A_PRESSED);
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
            return objective_obtain_abz_button_challenges(objective, update, BINGO_UPDATE_B_PRESSED);
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
            return objective_obtain_abz_button_challenges(objective, update, BINGO_UPDATE_Z_PRESSED);
        case BINGO_OBJECTIVE_RANDOM_RED_COINS:
            return objective_obtain_star_random_reds(objective, update);
        case BINGO_OBJECTIVE_STAR_TIMED:
            return objective_obtain_star_timer(objective, update);
        case BINGO_OBJECTIVE_STAR_TTC_RANDOM:
            return objective_obtain_ttc_random_star(objective, update);
        case BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK:
            return objective_obtain_star_reverse_joystick(objective, update);
        case BINGO_OBJECTIVE_STAR_GREEN_DEMON:
            return objective_obtain_star_greendemon(objective, update);
        case BINGO_OBJECTIVE_STAR_CLICK_GAME:
            return objective_obtain_star_click_game(objective, update);
        case BINGO_OBJECTIVE_STAR_DAREDEVIL:
            return objective_obtain_star_daredevil(objective, update);
        case BINGO_OBJECTIVE_COIN:
            return objective_obtain_coins(objective, update);
        case BINGO_OBJECTIVE_SPLATOON:
            return objective_splatoon(objective, update);
        case BINGO_OBJECTIVE_MULTICOIN:
            return objective_obtain_multicoin(objective, update);
        case BINGO_OBJECTIVE_MULTISTAR:
            return objective_obtain_multistar(objective, update);
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
            return objective_obtain_1ups_in_level(objective, update);
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
            return objective_obtain_stars_in_level(objective, update);
        case BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS:
            return objective_dangerous_wall_kicks(objective, update);
        case BINGO_OBJECTIVE_LOSE_MARIO_HAT:
            return objective_lose_mario_hat(objective, update);
        case BINGO_OBJECTIVE_BLJ:
            return objective_blj(objective, update);
        case BINGO_OBJECTIVE_RACING_STARS:
            return objective_racing_stars(objective, update);
        case BINGO_OBJECTIVE_SECRETS_STARS:
            return objective_secrets_stars(objective, update);
        case BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS:
            return objective_stars_multiple_levels(objective, update);
        case BINGO_OBJECTIVE_BOWSER:
            return objective_bowser(objective, update);
        case BINGO_OBJECTIVE_ROOF_WITHOUT_CANNON:
            return objective_roof_without_cannon(objective, update);
        case BINGO_OBJECTIVE_SIGNPOST:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_READ_SIGNPOST);
        case BINGO_OBJECTIVE_POLES:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_GRABBED_POLE);
        case BINGO_OBJECTIVE_SHOOT_CANNONS:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_CANNON_COLLECTABLE);
        case BINGO_OBJECTIVE_RED_COIN:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_RED_COIN);
        case BINGO_OBJECTIVE_EXCLAMATION_MARK_BOX:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_EXCLAMATION_MARK_BOX);
        case BINGO_OBJECTIVE_WING_CAP_BOX:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_WING_CAP_BOX);
        case BINGO_OBJECTIVE_VANISH_CAP_BOX:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_VANISH_CAP_BOX);
        case BINGO_OBJECTIVE_METAL_CAP_BOX:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_METAL_CAP_BOX);
        case BINGO_OBJECTIVE_AMPS:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_ZAPPED_BY_AMP);
        case BINGO_OBJECTIVE_KILL_GOOMBAS:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_KILLED_GOOMBA);
        case BINGO_OBJECTIVE_KILL_BOBOMBS:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_KILLED_BOBOMB);
        case BINGO_OBJECTIVE_KILL_SPINDRIFTS:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_KILLED_SPINDRIFT);
        case BINGO_OBJECTIVE_KILL_MR_IS:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_KILLED_MR_I);
        case BINGO_OBJECTIVE_KILL_SCUTTLEBUGS:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_KILLED_SCUTTLEBUG);
        case BINGO_OBJECTIVE_KILL_BULLIES:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_KILLED_BULLY);
        case BINGO_OBJECTIVE_KILL_CHUCKYAS:
            return objective_generic_collectable(objective, update, BINGO_UPDATE_KILLED_CHUCKYA);
    }
}