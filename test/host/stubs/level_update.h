#ifndef HOST_STUB_LEVEL_UPDATE_H
#define HOST_STUB_LEVEL_UPDATE_H

#include <ultra64.h>

extern s16 gTTCSpeedSetting;

// Mirrors the fields bingo code reads from the real struct.
struct HudDisplay {
    s16 lives;
    s16 coins;
    s16 stars;
    s16 wedges;
    s16 keys;
    s16 flags;
    u16 timer;
};

extern struct HudDisplay gHudDisplay;

#endif
