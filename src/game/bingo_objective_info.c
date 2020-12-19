#include <ultra64.h>
#include "bingo.h"
#include "bingo_objective_info.h"
#include "text_strings.h"

static struct BingoObjectiveInfo sBingoObjectiveInfo[] = {
    { BINGO_OBJECTIVE_STAR, BINGO_ICON_STAR, { TEXT_SINGLE_STAR } },
    { BINGO_OBJECTIVE_STAR_TIMED, BINGO_ICON_STAR_TIMED, { TEXT_TIMED_STAR } },
    { BINGO_OBJECTIVE_STAR_TTC_RANDOM, BINGO_ICON_STAR_TTC_RANDOM, { TEXT_TTC_RANDOM } },
    { BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE, BINGO_ICON_STAR_A_BUTTON_CHALLENGE, { TEXT_A_BUTTON } },
    { BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE, BINGO_ICON_STAR_B_BUTTON_CHALLENGE, { TEXT_B_BUTTON } },
    { BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE, BINGO_ICON_STAR_Z_BUTTON_CHALLENGE, { TEXT_Z_BUTTON } },
    { BINGO_OBJECTIVE_STAR_CLICK_GAME, BINGO_ICON_STAR_CLICK_GAME, { TEXT_CLICK_GAME } },
    { BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK, BINGO_ICON_STAR_REVERSE_JOYSTICK, { TEXT_REVERSE_JOYSTICK } },
    { BINGO_OBJECTIVE_STAR_GREEN_DEMON, BINGO_ICON_STAR_GREEN_DEMON, { TEXT_GREEN_DEMON } },
    { BINGO_OBJECTIVE_STAR_DAREDEVIL, BINGO_ICON_STAR_DAREDEVIL, { TEXT_DAREDEVIL } },
    { BINGO_OBJECTIVE_COIN, BINGO_ICON_COIN, { TEXT_COIN_LEVEL } },
    { BINGO_OBJECTIVE_1UPS_IN_LEVEL, BINGO_ICON_1UPS_IN_LEVEL, { TEXT_1UP_LEVEL } },
    { BINGO_OBJECTIVE_STARS_IN_LEVEL, BINGO_ICON_STARS_IN_LEVEL, { TEXT_STARS_LEVEL } },
    { BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS, BINGO_ICON_DANGEROUS_WALL_KICKS, { TEXT_DANGEROUS_WALL_KICKS } },
    { BINGO_OBJECTIVE_LOSE_MARIO_HAT, BINGO_ICON_MARIO_HAT, { TEXT_MARIO_HAT } },
    { BINGO_OBJECTIVE_BLJ, BINGO_ICON_BLJ, { TEXT_BLJ } },
    { BINGO_OBJECTIVE_BOWSER, BINGO_ICON_BOWSER, { TEXT_BOWSER } },
    { BINGO_OBJECTIVE_MULTICOIN, BINGO_ICON_MULTICOIN, { TEXT_TOTAL_COIN } },
    { BINGO_OBJECTIVE_MULTISTAR, BINGO_ICON_MULTISTAR, { TEXT_MULTI_STAR } },
    { BINGO_OBJECTIVE_SIGNPOST, BINGO_ICON_SIGNPOST, { TEXT_SIGNPOSTS } },
    { BINGO_OBJECTIVE_RED_COIN, BINGO_ICON_RED_COIN, { TEXT_RED_COINS } },
    { BINGO_OBJECTIVE_EXCLAMATION_MARK_BOX, BINGO_ICON_EXCLAMATION_MARK_BOX, { TEXT_EXCLAM_BOXES } },
    { BINGO_OBJECTIVE_AMPS, BINGO_ICON_AMP, { TEXT_AMPS } },
    { BINGO_OBJECTIVE_KILL_GOOMBAS, BINGO_ICON_KILL_GOOMBAS, { TEXT_KILL_GOOMBAS } },
    { BINGO_OBJECTIVE_KILL_BOBOMBS, BINGO_ICON_KILL_BOBOMBS, { TEXT_KILL_BOBOMBS } },
    { BINGO_OBJECTIVE_KILL_SPINDRIFTS, BINGO_ICON_KILL_SPINDRIFTS, { TEXT_KILL_SPINDRIFTS } },
    { BINGO_OBJECTIVE_KILL_MR_IS, BINGO_ICON_KILL_MR_IS, { TEXT_KILL_MR_IS } },
};

struct BingoObjectiveInfo *get_objective_info(enum BingoObjectiveType type) {
    s32 i;
    s32 size = sizeof(sBingoObjectiveInfo) / sizeof(sBingoObjectiveInfo[0]);
    for (i = 0; i < size; i++) {
        if (sBingoObjectiveInfo[i].type == type) {
            return &sBingoObjectiveInfo[i];
        }
    }
    return NULL;
}
