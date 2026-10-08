#ifndef _BINGO_DAILY_H
#define _BINGO_DAILY_H

#include <PR/ultratypes.h>

// The daily board: one seed and one set of rules per UTC day, the same for
// everyone. The rules follow a fixed weekly rotation so a day is easy to
// remember ("blackout Sunday"); only the seed changes week to week.
struct BingoDaily {
    u32 seed;    // 1..999999999 (the seed entry holds 9 digits)
    u8 preset;   // enum BingoPresetId: which tiles are in the deck
    u8 mode;     // enum BingoGameMode, always a solo-playable one
    u8 nonstop;
};

// day = days since 1970-01-01 UTC.
void bingo_daily_for_day(u32 day, struct BingoDaily *out);
// "BLACKOUT VANILLA", "1 LINE SRL NONSTOP", ... (menu font: caps, digits).
void bingo_daily_describe(const struct BingoDaily *daily, char *out, s32 size);

#endif /* _BINGO_DAILY_H */
