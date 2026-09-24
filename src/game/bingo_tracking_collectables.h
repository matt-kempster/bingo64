#ifndef _BINGO_TRACKING_COLLECTABLES
#define _BINGO_TRACKING_COLLECTABLES

#include <ultra64.h>
#include "area.h"
#include "bingo.h"

void bingo_tracking_collectables_reset(void);
u32 get_unique_id(enum BingoObjectiveUpdate, f32 posX, f32 posY, f32 posZ);
s32 is_new_kill(enum BingoObjectiveUpdate type, u32 uid);
s32 peek_would_be_new_kill(enum BingoObjectiveUpdate type, u32 uid);
s32 bingo_count_unique_source(enum BingoObjectiveUpdate update, f32 x, f32 y, f32 z);
s32 bingo_track_warp_pad(s32 area, s32 nodeA, s32 nodeB);

#endif /* _BINGO_TRACKING_COLLECTABLES */