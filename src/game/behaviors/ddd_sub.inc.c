// ddd_sub.inc.c
#include "game/bingo.h"

void bhv_bowsers_sub_loop(void) {
    s32 shouldDelete = 0;
    if (gBingoFullGameUnlocked) {
        shouldDelete = (gCurrActNum != 1);
    } else {
        shouldDelete = save_file_get_flags() & (SAVE_FLAG_HAVE_KEY_2 | SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR);
    }
    if (shouldDelete) {
        obj_mark_for_deletion(o);
    }
}
