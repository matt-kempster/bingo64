#include <ultra64.h>
#include "macros.h"
#include "bingo.h"
#include "bingo_objective_info.h"
#include "text_strings.h"

ALIGNED8 static const u8 seg2_texture_goomba[] = {
#include "textures/segment2/custom/segment2.goomba.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_abutton[] = {
#include "textures/segment2/custom/segment2.abutton.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_greendemon[] = {
#include "textures/segment2/custom/segment2.greendemon.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_rbutton[] = {
#include "textures/segment2/custom/segment2.rbutton.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_daredevil[] = {
#include "textures/segment2/custom/segment2.daredevil.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_randomreds[] = {
#include "textures/segment2/custom/segment2.randomreds.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_joystick[] = {
#include "textures/segment2/custom/segment2.joystick.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_timer[] = {
#include "textures/segment2/custom/segment2.timer.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_ttc_random[] = {
#include "textures/segment2/custom/segment2.ttc_random.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_1up[] = {
#include "textures/segment2/custom/segment2.1up.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_allstarslevel[] = {
#include "textures/segment2/custom/segment2.allstarslevel.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_lightning[] = {
#include "textures/segment2/custom/segment2.lightning.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_wallkick[] = {
#include "textures/segment2/custom/segment2.wallkick.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_mario_hat[] = {
#include "textures/segment2/custom/segment2.mario_hat.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_blj[] = {
#include "textures/segment2/custom/segment2.blj.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_racing[] = {
#include "textures/segment2/custom/segment2.racing.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_secrets[] = {
#include "textures/segment2/custom/segment2.secret.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_bowser[] = {
#include "textures/segment2/custom/segment2.bowser.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_red_coin[] = {
#include "textures/segment2/custom/segment2.redcoin.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_signpost[] = {
#include "textures/segment2/custom/segment2.signpost.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_amp[] = {
#include "textures/segment2/custom/segment2.amp.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_bobomb[] = {
#include "textures/segment2/custom/segment2.bobomb.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_spindrift[] = {
#include "textures/segment2/custom/segment2.spindrift.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_mr_i[] = {
#include "textures/segment2/custom/segment2.mr_i.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_scuttlebug[] = {
#include "textures/segment2/custom/segment2.scuttlebug.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_bully[] = {
#include "textures/segment2/custom/segment2.bully.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_chuckya[] = {
#include "textures/segment2/custom/segment2.chuckya.rgba16.inc.c"
};

ALIGNED8 static const u8 texture_hud_char_star[] = {
#include "textures/segment2/segment2.05C00.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_bbutton[] = {
#include "textures/segment2/custom/segment2.bbutton.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_zbutton[] = {
#include "textures/segment2/custom/segment2.zbutton.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_multicoin[] = {
#include "textures/segment2/custom/segment2.coins.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_multistar[] = {
#include "textures/segment2/custom/segment2.multistar.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_yellowbox[] = {
#include "textures/segment2/custom/segment2.yellowbox.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_wingbox[] = {
#include "textures/segment2/custom/segment2.wingbox.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_vanishbox[] = {
#include "textures/segment2/custom/segment2.vanishbox.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_metalbox[] = {
#include "textures/segment2/custom/segment2.metalbox.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_pole[] = {
#include "textures/segment2/custom/segment2.pole.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_roofwithoutcannon[] = {
#include "textures/segment2/custom/segment2.roofwithoutcannon.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_cannon[] = {
#include "textures/segment2/custom/segment2.cannon.rgba16.inc.c"
};

ALIGNED8 static const u8 texture_hud_char_coin[] = {
#include "textures/segment2/segment2.05800.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_splatoon[] = {
#include "textures/segment2/custom/segment2.splatoon.rgba16.inc.c"
};

ALIGNED8 static const u8 seg2_texture_skull[] = {
#include "textures/segment2/custom/segment2.skull.rgba16.inc.c"
};

struct BingoObjectiveInfo sBingoObjectiveInfo[] = {
    { BINGO_OBJECTIVE_STAR, BINGO_ICON_STAR, { TEXT_SINGLE_STAR }, texture_hud_char_star },
    { BINGO_OBJECTIVE_STAR_TIMED, BINGO_ICON_STAR_TIMED, { TEXT_TIMED_STAR }, seg2_texture_timer },
    { BINGO_OBJECTIVE_STAR_TTC_RANDOM, BINGO_ICON_STAR_TTC_RANDOM, { TEXT_TTC_RANDOM }, seg2_texture_ttc_random },
    { BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE, BINGO_ICON_STAR_A_BUTTON_CHALLENGE, { TEXT_A_BUTTON }, seg2_texture_abutton },
    { BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE, BINGO_ICON_STAR_B_BUTTON_CHALLENGE, { TEXT_B_BUTTON }, seg2_texture_bbutton },
    { BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE, BINGO_ICON_STAR_Z_BUTTON_CHALLENGE, { TEXT_Z_BUTTON }, seg2_texture_zbutton },
    { BINGO_OBJECTIVE_STAR_CLICK_GAME, BINGO_ICON_STAR_CLICK_GAME, { TEXT_CLICK_GAME }, seg2_texture_rbutton },
    { BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK, BINGO_ICON_STAR_REVERSE_JOYSTICK, { TEXT_REVERSE_JOYSTICK }, seg2_texture_joystick },
    { BINGO_OBJECTIVE_STAR_GREEN_DEMON, BINGO_ICON_STAR_GREEN_DEMON, { TEXT_GREEN_DEMON }, seg2_texture_greendemon },
    { BINGO_OBJECTIVE_STAR_DAREDEVIL, BINGO_ICON_STAR_DAREDEVIL, { TEXT_DAREDEVIL }, seg2_texture_daredevil },
    { BINGO_OBJECTIVE_RANDOM_RED_COINS, BINGO_ICON_RANDOM_RED_COINS, { TEXT_RANDOM_REDS }, seg2_texture_randomreds },
    { BINGO_OBJECTIVE_COIN, BINGO_ICON_COIN, { TEXT_COIN_LEVEL }, texture_hud_char_coin },
    { BINGO_OBJECTIVE_SPLATOON, BINGO_ICON_SPLATOON, { TEXT_SPLATOON_LEVEL }, seg2_texture_splatoon },
    { BINGO_OBJECTIVE_1UPS_IN_LEVEL, BINGO_ICON_1UPS_IN_LEVEL, { TEXT_1UP_LEVEL }, seg2_texture_1up },
    { BINGO_OBJECTIVE_STARS_IN_LEVEL, BINGO_ICON_STARS_IN_LEVEL, { TEXT_STARS_LEVEL }, seg2_texture_allstarslevel },
    { BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS, BINGO_ICON_STARS_MULTIPLE_LEVELS, { TEXT_STARS_MULTI_LEVELS }, seg2_texture_lightning },
    { BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS, BINGO_ICON_DANGEROUS_WALL_KICKS, { TEXT_DANGEROUS_WALL_KICKS }, seg2_texture_wallkick },
    { BINGO_OBJECTIVE_LOSE_MARIO_HAT, BINGO_ICON_MARIO_HAT, { TEXT_MARIO_HAT }, seg2_texture_mario_hat },
    { BINGO_OBJECTIVE_BLJ, BINGO_ICON_BLJ, { TEXT_BLJ }, seg2_texture_blj },
    { BINGO_OBJECTIVE_RACING_STARS, BINGO_ICON_RACING_STARS, { TEXT_RACING }, seg2_texture_racing },
    { BINGO_OBJECTIVE_SECRETS_STARS, BINGO_ICON_SECRETS_STARS, { TEXT_SECRETS }, seg2_texture_secrets },
    { BINGO_OBJECTIVE_BOWSER, BINGO_ICON_BOWSER, { TEXT_BOWSER }, seg2_texture_bowser },
    { BINGO_OBJECTIVE_ROOF_WITHOUT_CANNON, BINGO_ICON_ROOF_WITHOUT_CANNON, { TEXT_ROOF_WITHOUT_CANNON }, seg2_texture_roofwithoutcannon },
    { BINGO_OBJECTIVE_SHOOT_CANNONS, BINGO_ICON_CANNON, { TEXT_CANNONS }, seg2_texture_cannon },
    { BINGO_OBJECTIVE_MULTICOIN, BINGO_ICON_MULTICOIN, { TEXT_TOTAL_COIN }, seg2_texture_multicoin },
    { BINGO_OBJECTIVE_MULTISTAR, BINGO_ICON_MULTISTAR, { TEXT_MULTI_STAR }, seg2_texture_multistar },
    { BINGO_OBJECTIVE_SIGNPOST, BINGO_ICON_SIGNPOST, { TEXT_SIGNPOSTS }, seg2_texture_signpost },
    { BINGO_OBJECTIVE_POLES, BINGO_ICON_POLE, { TEXT_POLES }, seg2_texture_pole},
    { BINGO_OBJECTIVE_RED_COIN, BINGO_ICON_RED_COIN, { TEXT_RED_COINS }, seg2_texture_red_coin },
    { BINGO_OBJECTIVE_EXCLAMATION_MARK_BOX, BINGO_ICON_EXCLAMATION_MARK_BOX, { TEXT_EXCLAM_BOXES }, seg2_texture_yellowbox },
    { BINGO_OBJECTIVE_WING_CAP_BOX, BINGO_ICON_WING_CAP_BOX, { TEXT_WING_BOXES }, seg2_texture_wingbox },
    { BINGO_OBJECTIVE_VANISH_CAP_BOX, BINGO_ICON_VANISH_CAP_BOX, { TEXT_VANISH_BOXES }, seg2_texture_vanishbox },
    { BINGO_OBJECTIVE_METAL_CAP_BOX, BINGO_ICON_METAL_CAP_BOX, { TEXT_METAL_BOXES }, seg2_texture_metalbox },
    { BINGO_OBJECTIVE_RECOVERY_HEART, BINGO_ICON_RECOVERY_HEART, { TEXT_RECOVERY_HEARTS }, seg2_texture_1up },
    { BINGO_OBJECTIVE_AMPS, BINGO_ICON_AMP, { TEXT_AMPS }, seg2_texture_amp },
    { BINGO_OBJECTIVE_KILL_GOOMBAS, BINGO_ICON_KILL_GOOMBAS, { TEXT_KILL_GOOMBAS }, seg2_texture_goomba },
    { BINGO_OBJECTIVE_KILL_BOBOMBS, BINGO_ICON_KILL_BOBOMBS, { TEXT_KILL_BOBOMBS }, seg2_texture_bobomb },
    { BINGO_OBJECTIVE_KILL_SPINDRIFTS, BINGO_ICON_KILL_SPINDRIFTS, { TEXT_KILL_SPINDRIFTS }, seg2_texture_spindrift },
    { BINGO_OBJECTIVE_KILL_MR_IS, BINGO_ICON_KILL_MR_IS, { TEXT_KILL_MR_IS }, seg2_texture_mr_i },
    { BINGO_OBJECTIVE_KILL_SCUTTLEBUGS, BINGO_ICON_KILL_SCUTTLEBUGS, { TEXT_KILL_SCUTTLEBUGS }, seg2_texture_scuttlebug },
    { BINGO_OBJECTIVE_KILL_BULLIES, BINGO_ICON_KILL_BULLIES, { TEXT_KILL_BULLIES }, seg2_texture_bully },
    { BINGO_OBJECTIVE_KILL_CHUCKYAS, BINGO_ICON_KILL_CHUCKYAS, { TEXT_KILL_CHUCKYAS }, seg2_texture_chuckya },
    { BINGO_OBJECTIVE_UNIQUE_DEATHS, BINGO_ICON_UNIQUE_DEATHS, { TEXT_UNIQUE_DEATHS }, seg2_texture_skull }
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

struct BingoObjectiveInfo *get_objective_info_from_icon(enum BingoObjectiveIcon icon) {
    s32 i;
    s32 size = sizeof(sBingoObjectiveInfo) / sizeof(sBingoObjectiveInfo[0]);
    for (i = 0; i < size; i++) {
        if (sBingoObjectiveInfo[i].icon == icon) {
            return &sBingoObjectiveInfo[i];
        }
    }
    return NULL;
}