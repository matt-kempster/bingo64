// Fake versions of the game parts that the bingo logic touches.
// The tests only care about bingo logic, so these can be very simple.

#include <ultra64.h>
#include "types.h"

s16 gCurrCourseNum = 0;
s16 gCurrLevelNum = 0;
s16 gTTCSpeedSetting = 0;
s16 sSelectionFlags = 0;
s8 gDialogCameraAngleIndex = 0;
f32 gDefaultSoundArgs[3] = { 0 };

const BehaviorScript bhv1upGreenDemon[] = { 0 };

// The real tables hold in-game-encoded course and act names.
// 0xFF means "end of string", so every fake name is just empty.
static u8 sFakeEncodedName[] = { 0xFF };
u8 *seg2_act_name_table_lowercase[25 * 6];
u8 *seg2_course_name_table_lowercase[25];

void *segmented_to_virtual(void *addr) {
    return addr;
}

static void init_fake_name_tables(void) __attribute__((constructor));
static void init_fake_name_tables(void) {
    int i;
    for (i = 0; i < 25 * 6; i++) {
        seg2_act_name_table_lowercase[i] = sFakeEncodedName;
    }
    for (i = 0; i < 25; i++) {
        seg2_course_name_table_lowercase[i] = sFakeEncodedName;
    }
}

s32 gGluePlayedSounds = 0;

void play_sound(s32 soundBits, f32 *pos) {
    (void) soundBits;
    (void) pos;
    gGluePlayedSounds++;
}

void bingo_hud_update_message(s32 icon, char message[10], s8 horribleHackWallkick) {
    (void) icon;
    (void) message;
    (void) horribleHackWallkick;
}

void bingo_hud_update_number(s32 icon, s32 number) {
    (void) icon;
    (void) number;
}

void bingo_hud_update_state(s32 icon, s32 state) {
    (void) icon;
    (void) state;
}

struct Object *obj_nearest_object_with_behavior(const BehaviorScript *behavior) {
    (void) behavior;
    return NULL;
}

void mark_object_for_deletion(struct Object *obj) {
    (void) obj;
}

// Same as the game's RandomU16 in behavior_script.c: low 16 bits of the
// Mersenne Twister output.
unsigned long genrand_int32(void);

u32 RandomU32(void) {
    return genrand_int32();
}

u16 RandomU16(void) {
    return (u16) RandomU32();
}
