#ifndef HUD_H
#define HUD_H

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "types.h"
#include "bingo.h"

#ifdef VERSION_JP
#define HUD_TOP_Y 210
#else
#define HUD_TOP_Y 209
#endif

/* Segment 3 */
extern u8 *power_meter_seg3_health_icons_030293E0[];
extern Gfx* power_meter_seg3_dl_03029480;
extern Gfx* power_meter_seg3_dl_03029570;
extern Gfx* power_meter_seg3_dl_030295A0;

enum PowerMeterAnimation {
    POWER_METER_HIDDEN,
    POWER_METER_EMPHASIZED,
    POWER_METER_DEEMPHASIZING,
    POWER_METER_HIDING,
    POWER_METER_VISIBLE
};

enum CameraHUDLut {
    GLYPH_CAM_CAMERA,
    GLYPH_CAM_MARIO_HEAD,
    GLYPH_CAM_LAKITU_HEAD,
    GLYPH_CAM_FIXED,
    GLYPH_CAM_ARROW_UP,
    GLYPH_CAM_ARROW_DOWN
};

// Functions
void set_hud_camera_status(s16 status);
void render_hud(void);
extern void bingo_hud_update_message(enum BingoObjectiveIcon, char message[10], s8 horribleHackWallkick);
extern void bingo_hud_update_number(enum BingoObjectiveIcon, s32);
extern void bingo_hud_update_state(enum BingoObjectiveIcon, enum BingoObjectiveIcon);

#endif // HUD_H
