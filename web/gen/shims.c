// Web build glue: the same fakes as test/host/glue.c, minus the test
// counters, and with one big difference: the course and act name tables are
// the real ones from the generated text data, so objective descriptions on
// the website show the real level and star names.

#include <ultra64.h>
#include "macros.h" // for GLUE2, used by the generated text data below
#include "types.h"

s16 gCurrCourseNum = 0;
s16 gCurrLevelNum = 0;
s16 gTTCSpeedSetting = 0;
s16 sSelectionFlags = 0;
s8 gDialogCameraAngleIndex = 0;
f32 gGlobalSoundSource[3] = { 0 };

const BehaviorScript bhv1upGreenDemon[] = { 0 };

void *segmented_to_virtual(void *addr) {
    return addr;
}

void play_sound(s32 soundBits, f32 *pos) {
    (void) soundBits;
    (void) pos;
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

struct Object *cur_obj_nearest_object_with_behavior(const BehaviorScript *behavior) {
    (void) behavior;
    return NULL;
}

void obj_mark_for_deletion(struct Object *obj) {
    (void) obj;
}

// Netplay hooks in bingo.c: the web generator is always offline.
struct BingoObjective;

void bingo_net_on_local_complete(struct BingoObjective *objective) {
    (void) objective;
}

s32 bingo_net_racing(void) {
    return 0;
}
s32 bingo_net_race_decided(void) {
    return 0;
}
s32 bingo_net_local_won(void) {
    return 0;
}

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

// The real course/act name tables. The main ROM build generates this file
// with the in-game encoded strings already substituted, so descriptions can
// decode them with reverse_encode_str exactly like the game does.
struct DialogEntry {
    u32 unused;
    s8 linesPerBox;
    s16 leftOffset;
    s16 width;
    const u8 *str;
};

#include "text/us/define_text.inc.c"
