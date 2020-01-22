#include <ultra64.h>
#include <PR/os_libc.h>

#include "area.h"
#include "bingo.h"

#include "bingo_descriptions.h"
#include "bingo_objective_func.h"
#include "bingo_objective_init.h"
#include "memory.h"
#include "segment2.h"
#include "strcpy.h"

char reverse_encode(u8 c) {
    if (c >= ('A' - 0x37) && c <= ('Z' - 0x37)) {
        return (c + 0x37);
    }

    if (c >= ('a' - 0x3D) && c <= ('z' - 0x3D)) {
        return (c + 0x3D);
    }

    if (c >= ('0' - 0x30) && c <= ('9' - 0x30)) {
        return (c + 0x30);
    }

    if (c == 0x9E) {
        return ' ';
    }
    if (c == 0x3E) {
        return '\'';
    }
    if (c == 0xF2) {
        return '!';
    }
    if (c == 0x9F) {
        return '-';
    }
    if (c == 0x24) {
        return '.';
    }
    if (c == 'o') {
        return ',';
    }
    if (c == 0xFE) {
        return '\n';
    }
    if (c == 0xE6) {
        return ':';
    }

    return c; // lol idk
}

void reverse_encode_str(u8 *str, char *buf) {
    int iter = 0;
    while (str[iter] != 0xFF) {
        buf[iter] = reverse_encode(str[iter]);
        iter++;
    }
    buf[iter] = '\0';
}

void get_act_name(char *buf, enum CourseNum course, s32 star) {
    void **actNameTbl = segmented_to_virtual(seg2_act_name_table);
    u8 *actName = segmented_to_virtual(actNameTbl[(course - 1) * 6 + star]);
    if (star == 6) {
        strcpy(buf, "100 Coins");
    } else {
        reverse_encode_str(actName, buf);
    }
}

void get_level_name(char *buf, enum CourseNum course) {
    void **levelNameTbl = segmented_to_virtual(seg2_course_name_table);
    u8 *levelName = segmented_to_virtual(levelNameTbl[course - 1]);
    reverse_encode_str(levelName, buf);
}

void getTimeFmtTiny(char *buf, int timestamp) {
    int min = timestamp / 1800;
    int sec = (timestamp % 1800) / 30;
    int tensec = sec / 10;
    int onesec = sec % 10;

    buf[0] = min + 0x30;
    buf[1] = ':';
    buf[2] = tensec + 0x30;
    buf[3] = onesec + 0x30;
    buf[4] = '\0';
}

void getTimeFmt(char *buf, s32 timestamp) {
    int min = timestamp / 1800;
    int sec = (timestamp % 1800) / 30;
    int tensec = sec / 10;
    int onesec = sec % 10;
    s32 i = 0;

    if (min > 9) {
        buf[i++] = (min / 10) + 0x30;
    }
    buf[i++] = (min % 10) + 0x30;
    buf[i++] = '[';
    buf[i++] = tensec + 0x30;
    buf[i++] = onesec + 0x30;
    buf[i++] = ']';
    buf[i++] = '\0';
}

////////////////////////////////////////////////////////////////////////////////

void get_star_objective_desc(struct BingoObjective *obj, char *desc) {
    char revEncLevelName[60];
    char revEncActName[60];
    char noTouchButtonText[60];
    char hintOrCompleteSuffix[200];
    s32 shouldPutSpace = 1;

    get_level_name(revEncLevelName, obj->data.starObjective.course);

    if (obj->data.starObjective.course == COURSE_PSS) {
        switch (obj->data.starObjective.starIndex) {
            case PSS_STAR_FAST:
                strcpy(revEncActName, "FAST");
                break;
            case PSS_STAR_SLOW:
                strcpy(revEncActName, "SLOW");
                break;
        }
    } else if (COURSE_IS_MAIN_COURSE(obj->data.starObjective.course)) {
        get_act_name(revEncActName, obj->data.starObjective.course, obj->data.starObjective.starIndex);
    } else {
        strcpy(revEncActName, "");
        shouldPutSpace = 0;
    }

    switch (obj->type) {
        case BINGO_OBJECTIVE_STAR:
            strcpy(noTouchButtonText, "");
            break;
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
            strcpy(noTouchButtonText, " without touching the A button");
            break;
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
            strcpy(noTouchButtonText, " without touching the B button");
            break;
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
            strcpy(noTouchButtonText, " without touching the Z trigger");
            break;
    }

    if (obj->state == BINGO_STATE_COMPLETE) {
        sprintf(hintOrCompleteSuffix, ": Complete!");
    } else if (obj->type == BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE) {
        if (obj->data.abcStarObjective.hint[0] == '\0') {
            sprintf(hintOrCompleteSuffix, "");
        } else {
            sprintf(hintOrCompleteSuffix, "\n(Hint: %s)", obj->data.abcStarObjective.hint);
        }
    } else {
        sprintf(hintOrCompleteSuffix, "");
    }

    sprintf(desc, "Collect the star%s%s from %s%s%s", shouldPutSpace ? " " : "", revEncActName,
            revEncLevelName + 3, noTouchButtonText, hintOrCompleteSuffix);
}

void get_star_timed_objective_desc(struct BingoObjective *obj, char *desc) {
    char revEncLevelName[60];
    char revEncActName[60];
    char total_timestamp[5];
    char rem_timestamp[5];
    char suffix[20];

    get_act_name(revEncActName, obj->data.starTimerObjective.course,
                 obj->data.starTimerObjective.starIndex);
    get_level_name(revEncLevelName, obj->data.starTimerObjective.course);

    getTimeFmtTiny(total_timestamp, obj->data.starTimerObjective.maxTime);

    if (obj->state == BINGO_STATE_COMPLETE) {
        strcpy(suffix, ": Complete!");
        strcpy(rem_timestamp, "");
    } else if (gCurrCourseNum == obj->data.starTimerObjective.course) {
        getTimeFmtTiny(rem_timestamp,
                       obj->data.starTimerObjective.maxTime - obj->data.starTimerObjective.timer);
        strcpy(suffix, " Remaining: ");
        suffix[11] = 0x9E;
    } else {
        strcpy(suffix, "");
        strcpy(rem_timestamp, "");
    }

    sprintf(desc, "Collect the star %s from %s in less than %s%s%s", revEncActName, revEncLevelName + 3,
            total_timestamp, suffix, rem_timestamp);
}
void get_coin_objective_desc(struct BingoObjective *obj, char *desc) {
    char revEncLevelName[60];
    char suffix[30];

    get_level_name(revEncLevelName, obj->data.starObjective.course);

    if (obj->state == BINGO_STATE_COMPLETE) {
        strcpy(suffix, ": Complete!");
    } else {
        sprintf(suffix, ". Remaining: %d",
                obj->data.courseCollectableData.toGet - obj->data.courseCollectableData.gotten);
    }

    sprintf(desc, "Collect %d coins in %s%s", obj->data.courseCollectableData.toGet,
            revEncLevelName + 3, suffix);
}

void get_multicoin_objective_desc(struct BingoObjective *obj, char *desc) {
    char suffix[30];

    if (obj->state == BINGO_STATE_COMPLETE) {
        strcpy(suffix, ": Complete!");
    } else {
        sprintf(suffix, ". Remaining: %d",
                obj->data.collectableData.toGet - obj->data.collectableData.gotten);
    }

    sprintf(desc, "Collect %d coins total%s", obj->data.collectableData.toGet, suffix);
}

void get_1up_level_objective_desc(struct BingoObjective *obj, char *desc) {
    char revEncLevelName[60];
    char suffix[30];

    get_level_name(revEncLevelName, obj->data.starObjective.course);

    if (obj->state == BINGO_STATE_COMPLETE) {
        strcpy(suffix, ": Complete!");
    } else {
        sprintf(suffix, ". Remaining: %d",
                obj->data.courseCollectableData.toGet - obj->data.courseCollectableData.gotten);
    }

    sprintf(desc, "Collect all the 1up mushrooms in %s%s", revEncLevelName + 3, suffix);
}
void get_stars_in_level_objective_desc(struct BingoObjective *obj, char *desc) {
    char revEncLevelName[60];
    char suffix[30];

    get_level_name(revEncLevelName, obj->data.starObjective.course);
    if (obj->state == BINGO_STATE_COMPLETE) {
        strcpy(suffix, ": Complete!");
    } else {
        sprintf(suffix, ".");
    }
    sprintf(desc, "Collect all the stars in %s%s", revEncLevelName + 3, suffix);
}

void get_exclamation_mark_box_desc(struct BingoObjective *obj, char *desc) {
    char suffix[30];
    if (obj->state == BINGO_STATE_COMPLETE) {
        strcpy(suffix, ": Complete!");
    } else {
        sprintf(suffix, ". Remaining: %d",
                obj->data.collectableData.toGet - obj->data.collectableData.gotten);
    }

    sprintf(desc, "Break %d yellow ! boxes%s", obj->data.collectableData.toGet, suffix);
}

void get_kill_enemy_objective_desc(struct BingoObjective *obj, char *desc) {
    char suffix[30];
    char enemyName[15];

    if (obj->state == BINGO_STATE_COMPLETE) {
        strcpy(suffix, ": Complete!");
    } else {
        sprintf(suffix, ". Remaining: %d",
                obj->data.killEnemyObjective.enemiesToKill
                    - obj->data.killEnemyObjective.enemiesKilled);
    }

    switch (obj->data.killEnemyObjective.enemyType) {
        case ENEMY_TYPE_GOOMBA:
            strcpy(enemyName, "Goombas");
            break;
        case ENEMY_TYPE_BOBOMB:
            strcpy(enemyName, "Bob-ombs");
            break;
    }

    sprintf(desc, "Kill %d %s%s", obj->data.killEnemyObjective.enemiesToKill, enemyName, suffix);
}

void describe_objective(struct BingoObjective *objective, char *desc) {
    switch (objective->type) {
        case BINGO_OBJECTIVE_STAR:
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
            get_star_objective_desc(objective, desc);
            break;
        case BINGO_OBJECTIVE_STAR_TIMED:
            get_star_timed_objective_desc(objective, desc);
            break;
        case BINGO_OBJECTIVE_COIN:
            get_coin_objective_desc(objective, desc);
            break;
        case BINGO_OBJECTIVE_MULTICOIN:
            get_multicoin_objective_desc(objective, desc);
            break;
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
            get_1up_level_objective_desc(objective, desc);
            break;
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
            get_stars_in_level_objective_desc(objective, desc);
            break;
        case BINGO_OBJECTIVE_EXCLAMATION_MARK_BOX:
            get_exclamation_mark_box_desc(objective, desc);
            break;
        case BINGO_OBJECTIVE_KILL_ENEMIES:
            get_kill_enemy_objective_desc(objective, desc);
            break;
    }
}