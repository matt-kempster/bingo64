// bingo_crushers.c
//
// "Get crushed by N distinct crushers".
//
// Detection surface: Mario enters ACT_SQUISHED from exactly four sites, one per
// action-group cancel handler (airborne / stationary / moving / object). Each
// of them is `if (m->input & INPUT_SQUISHED) drop_and_set_mario_action(m,
// ACT_SQUISHED, 0);`, and bingo_track_crushed is called immediately before the
// drop. ACT_SQUISHED is a cutscene-group action with no squish cancel of its
// own, so a single crush fires this exactly once -- no per-frame dedup needed.
// The credit lands well before the squish death (which is 15+ frames later),
// so dying to the crush still counts.
//
// Attribution reads the CEILING side only. mario.c only raises INPUT_SQUISHED
// when a dynamic surface has pinched the ceil-floor gap to <= 150, and the
// crusher is by construction the thing overhead. Falling back to
// m->floor->object would be wrong: standing on a thwomp while a pyramid
// elevator descends would misattribute the crush to the thwomp.

#include <ultra64.h>

#include "sm64.h"
#include "types.h"
#include "area.h"
#include "behavior_data.h"
#include "bingo.h"
#include "bingo_crushers.h"
#include "bingo_tracking_collectables.h"
#include "object_fields.h"
#include "object_helpers.h"

// Owner-approved whitelist. Every one of these harms Mario by squishing and
// nothing else -- none carries a damage hitbox -- so the four ACT_SQUISHED
// entry sites are the complete detection surface for them.
static const BehaviorScript *sCrusherBehaviors[] = {
    bhvThwomp,
    bhvThwomp2,
    bhvGrindel,
    bhvHorizontalGrindel,
    bhvSpindel,
    bhvToxBox,
    bhvSmallWhomp,
    bhvWhompKingBoss,
};

#define NUM_CRUSHER_BEHAVIORS (s32)(sizeof(sCrusherBehaviors) / sizeof(sCrusherBehaviors[0]))

static s32 obj_is_whitelisted_crusher(struct Object *obj) {
    s32 i;

    for (i = 0; i < NUM_CRUSHER_BEHAVIORS; i++) {
        // obj_has_behavior segmented-to-virtual resolves the script pointer;
        // comparing obj->behavior against the raw symbol would not match.
        if (obj_has_behavior(obj, sCrusherBehaviors[i])) {
            return TRUE;
        }
    }

    return FALSE;
}

void bingo_register_crusher(struct Object *obj, f32 keyX, f32 keyY, f32 keyZ) {
    if (obj->oBingoCrushId == 0) {
        obj->oBingoCrushId =
            get_unique_id(BINGO_UPDATE_CRUSHED_BY_CRUSHER, keyX, keyY, keyZ);
    }
}

void bingo_track_crushed(struct MarioState *m) {
    struct Object *crusher;

    // mario.c guarantees m->ceil is non-NULL wherever INPUT_SQUISHED is set,
    // but the check is free and this runs from four separate call sites.
    crusher = (m->ceil != NULL) ? m->ceil->object : NULL;

    // NULL means static level geometry crushed Mario -- never credit that.
    if (crusher == NULL) {
        return;
    }

    if (!obj_is_whitelisted_crusher(crusher)) {
        return;
    }

    // An unregistered crusher would index UID slot 0, which is the table's
    // empty-slot sentinel. Skip rather than credit the wrong slot.
    if (crusher->oBingoCrushId == 0) {
        return;
    }

    if (is_new_kill(BINGO_UPDATE_CRUSHED_BY_CRUSHER, crusher->oBingoCrushId)) {
        bingo_update(BINGO_UPDATE_CRUSHED_BY_CRUSHER);
    }
}
