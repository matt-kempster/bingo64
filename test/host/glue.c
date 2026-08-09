// Fake versions of the game parts that the bingo logic touches.
// The tests only care about bingo logic, so these can be very simple.

#include <ultra64.h>
#include "types.h"

s16 gCurrCourseNum = 0;
s16 gCurrLevelNum = 0;
s16 gTTCSpeedSetting = 0;
s16 sSelectionFlags = 0;
s8 gDialogCameraAngleIndex = 0;
f32 gGlobalSoundSource[3] = { 0 };

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

// Recorded so tests can assert when a HUD toast was posted.
s32 gGlueHudNumberCalls = 0;
s32 gGlueHudNumberLast = -1;

void bingo_hud_update_number(s32 icon, s32 number) {
    (void) icon;
    gGlueHudNumberCalls++;
    gGlueHudNumberLast = number;
}

void bingo_hud_update_state(s32 icon, s32 state) {
    (void) icon;
    (void) state;
}

struct Object *cur_obj_nearest_object_with_behavior(const BehaviorScript *behavior) {
    (void) behavior;
    return NULL;
}

void obj_mark_for_deletion(struct Object *obj) {
    (void) obj;
}

// Splatoon lives in game/rendering code the tests don't build; the bingo
// objective only reads the painted counter.
s32 gSplatoonEnabled = 0;
s32 gSplatoonPaintedCount = 0;
s32 gSplatoonTotalFloors = 0;

void splatoon_clear(void) {
    gSplatoonPaintedCount = 0;
    gSplatoonTotalFloors = 0;
}

// Same as the game's random_u16 in behavior_script.c: low 16 bits of the
// Mersenne Twister output.
unsigned long genrand_int32(void);

u32 random_u32(void) {
    return genrand_int32();
}

u16 random_u16(void) {
    return (u16) random_u32();
}
