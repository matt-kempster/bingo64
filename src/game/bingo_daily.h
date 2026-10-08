#ifndef _BINGO_DAILY_H
#define _BINGO_DAILY_H

#include <PR/ultratypes.h>

// The daily board: one seed and one set of rules per UTC day, the same for
// everyone. Both come from a hash of the day number, so tomorrow's rules
// are a surprise.
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

// Results live in a local text file, one finished day per line:
//   <day> <YYYY-MM-DD> <seed> <frames> <rules>
// Only the first finish of a day is kept (a replay knows the board).
struct BingoDailyStats {
    s32 total;        // days finished
    s32 streak;       // consecutive days up to today (or yesterday, if
                      // today isn't finished yet: the streak is still alive)
    s32 best;         // longest streak ever
    s32 todayFrames;  // -1 = not finished today
};

// days[] need not be sorted or unique.
void bingo_daily_stats(const u32 *days, const s32 *frames, s32 count, u32 today,
                       struct BingoDailyStats *out);
// A missing file reads as no results.
void bingo_daily_load(const char *path, u32 today, struct BingoDailyStats *out);
// Appends the result unless that day is already in the file. Returns 1 if
// it wrote a line.
s32 bingo_daily_record(const char *path, u32 day, const struct BingoDaily *daily,
                       s32 frames);

#define BINGO_DAILY_FILENAME "bingo64_daily.txt"

// The day whose daily board is being played right now, 0 = none.
extern u32 gBingoDailyDay;

#endif /* _BINGO_DAILY_H */
