#ifndef HOST_STUB_HUD_H
#define HOST_STUB_HUD_H

#include <ultra64.h>
#include "bingo.h"

extern void bingo_hud_update_message(enum BingoObjectiveIcon, char message[10], s8 horribleHackWallkick);
extern void bingo_hud_update_number(enum BingoObjectiveIcon, s32);
extern void bingo_hud_update_state(enum BingoObjectiveIcon, enum BingoObjectiveIcon);

#endif
