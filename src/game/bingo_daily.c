#ifndef TARGET_N64  // needs a clock; the N64 has none
#include <PR/ultratypes.h>
#include <stdio.h>

#include "bingo.h"
#include "bingo_daily.h"

// Index 0 = Sunday. Lockout and Call and Response need opponents, so the
// rotation sticks to line counts and blackout. The SRL day uses the SRL
// deck (and its fresh file) with a solo win condition.
static const struct {
    u8 preset;
    u8 mode;
    u8 nonstop;
} sWeek[7] = {
    { BINGO_PRESET_VANILLA, BINGO_MODE_BLACKOUT, 0 },  // Sunday
    { BINGO_PRESET_VANILLA, BINGO_MODE_LINE_1, 0 },    // Monday
    { BINGO_PRESET_CASUAL, BINGO_MODE_LINE_2, 0 },     // Tuesday
    { BINGO_PRESET_SRL, BINGO_MODE_LINE_1, 0 },        // Wednesday
    { BINGO_PRESET_VANILLA, BINGO_MODE_LINE_1, 1 },    // Thursday
    { BINGO_PRESET_VANILLA, BINGO_MODE_LINE_3, 0 },    // Friday
    { BINGO_PRESET_CASUAL, BINGO_MODE_LINE_2, 1 },     // Saturday
};

void bingo_daily_for_day(u32 day, struct BingoDaily *out) {
    // 1970-01-01 was a Thursday.
    u32 weekday = (day + 4) % 7;
    // Integer hash (lowbias32) so neighbouring days get unrelated seeds.
    u32 h = day;
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;

    out->seed = h % 999999999U + 1;
    out->preset = sWeek[weekday].preset;
    out->mode = sWeek[weekday].mode;
    out->nonstop = sWeek[weekday].nonstop;
}

void bingo_daily_describe(const struct BingoDaily *daily, char *out, s32 size) {
    const char *mode;
    const char *preset;

    switch (daily->mode) {
        case BINGO_MODE_LINE_2:
            mode = "2 LINES";
            break;
        case BINGO_MODE_LINE_3:
            mode = "3 LINES";
            break;
        case BINGO_MODE_BLACKOUT:
            mode = "BLACKOUT";
            break;
        default:
            mode = "1 LINE";
            break;
    }
    switch (daily->preset) {
        case BINGO_PRESET_SRL:
            preset = "SRL";
            break;
        case BINGO_PRESET_CASUAL:
            preset = "CASUAL";
            break;
        default:
            preset = "VANILLA";
            break;
    }
    snprintf(out, size, "%s %s%s", mode, preset, daily->nonstop ? " NONSTOP" : "");
}
#endif
