#ifndef TARGET_N64  // needs a clock; the N64 has none
#include <PR/ultratypes.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#include "macros.h"
#include "bingo.h"
#include "bingo_daily.h"

u32 gBingoDailyDay = 0;

// Integer hash (lowbias32): neighbouring inputs give unrelated outputs.
static u32 daily_hash(u32 h) {
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

// Lockout and Call and Response need opponents, so a daily is a line count
// or a blackout. Blackout is a long sitting, so it is the rarest.
static const u8 sModes[] = {
    BINGO_MODE_LINE_1, BINGO_MODE_LINE_1, BINGO_MODE_LINE_1, BINGO_MODE_LINE_1,
    BINGO_MODE_LINE_1, BINGO_MODE_LINE_1,
    BINGO_MODE_LINE_2, BINGO_MODE_LINE_2, BINGO_MODE_LINE_2, BINGO_MODE_LINE_2,
    BINGO_MODE_LINE_3, BINGO_MODE_LINE_3,
    BINGO_MODE_BLACKOUT, BINGO_MODE_BLACKOUT,
};
// The SRL day uses the SRL deck (and its fresh file) with a solo win
// condition.
static const u8 sPresets[] = {
    BINGO_PRESET_VANILLA, BINGO_PRESET_VANILLA, BINGO_PRESET_CASUAL, BINGO_PRESET_SRL,
};

void bingo_daily_for_day(u32 day, struct BingoDaily *out) {
    // One independent draw per field.
    out->seed = daily_hash(day) % 999999999U + 1;
    out->mode = sModes[daily_hash(day ^ 0x6d6f6465U) % ARRAY_COUNT(sModes)];
    out->preset = sPresets[daily_hash(day ^ 0x70726573U) % ARRAY_COUNT(sPresets)];
    out->nonstop = daily_hash(day ^ 0x6e6f6e73U) % 4 == 0;
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

static int daily_cmp_u32(const void *a, const void *b) {
    u32 x = *(const u32 *) a, y = *(const u32 *) b;
    return (x > y) - (x < y);
}

#define DAILY_MAX_RESULTS 4096

void bingo_daily_stats(const u32 *days, const s32 *frames, s32 count, u32 today,
                       struct BingoDailyStats *out) {
    static u32 sorted[DAILY_MAX_RESULTS];
    s32 i, n = 0, run = 0;
    u32 prev = 0;

    out->total = 0;
    out->streak = 0;
    out->best = 0;
    out->todayFrames = -1;
    if (count > DAILY_MAX_RESULTS) {
        count = DAILY_MAX_RESULTS;
    }
    for (i = 0; i < count; i++) {
        sorted[n++] = days[i];
        if (days[i] == today && out->todayFrames < 0) {
            out->todayFrames = frames[i];
        }
    }
    qsort(sorted, n, sizeof(sorted[0]), daily_cmp_u32);
    for (i = 0; i < n; i++) {
        if (i > 0 && sorted[i] == prev) {
            continue;
        }
        run = (i > 0 && sorted[i] == prev + 1) ? run + 1 : 1;
        prev = sorted[i];
        out->total++;
        if (run > out->best) {
            out->best = run;
        }
    }
    // The last run counts as the current streak if it reaches today, or
    // yesterday with today still open.
    if (n > 0 && (prev == today || prev + 1 == today)) {
        out->streak = run;
    }
}

static s32 daily_read(const char *path, u32 *days, s32 *frames) {
    FILE *f = fopen(path, "r");
    char line[160];
    s32 n = 0;
    if (f == NULL) {
        return 0;
    }
    while (n < DAILY_MAX_RESULTS && fgets(line, sizeof(line), f) != NULL) {
        unsigned int day, seed;
        int fr;
        char date[16];
        if (sscanf(line, "%u %15s %u %d", &day, date, &seed, &fr) == 4) {
            days[n] = day;
            frames[n] = fr;
            n++;
        }
    }
    fclose(f);
    return n;
}

void bingo_daily_load(const char *path, u32 today, struct BingoDailyStats *out) {
    static u32 days[DAILY_MAX_RESULTS];
    static s32 frames[DAILY_MAX_RESULTS];
    s32 n = daily_read(path, days, frames);
    bingo_daily_stats(days, frames, n, today, out);
}

s32 bingo_daily_record(const char *path, u32 day, const struct BingoDaily *daily,
                       s32 frames) {
    static u32 days[DAILY_MAX_RESULTS];
    static s32 oldFrames[DAILY_MAX_RESULTS];
    s32 n = daily_read(path, days, oldFrames);
    s32 i;
    // Civil date from the day number (Howard Hinnant's days_from_civil,
    // inverted), so the file reads without a calendar.
    s32 z = (s32) day + 719468;
    s32 era = z / 146097;
    s32 doe = z - era * 146097;
    s32 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    s32 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    s32 mp = (5 * doy + 2) / 153;
    s32 d = doy - (153 * mp + 2) / 5 + 1;
    s32 m = mp < 10 ? mp + 3 : mp - 9;
    s32 y = yoe + era * 400 + (m <= 2);
    char rules[40];
    FILE *f;

    for (i = 0; i < n; i++) {
        if (days[i] == day) {
            return 0;
        }
    }
    f = fopen(path, "a");
    if (f == NULL) {
        return 0;
    }
    bingo_daily_describe(daily, rules, sizeof(rules));
    fprintf(f, "%u %04d-%02d-%02d %u %d %s\n", (unsigned int) day, y, m, d,
            (unsigned int) daily->seed, frames, rules);
    fclose(f);
    return 1;
}
#endif
