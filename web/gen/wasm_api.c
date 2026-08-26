// JavaScript-facing API for the board generator.
//
// Everything crosses the JS boundary as JSON text or raw texture bytes, so
// the JS side never depends on C struct layout. The generation entry point
// mirrors what the game does on file select: set the options, then call
// setup_bingo_objectives(seed).

#include <ultra64.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

#include "bingo.h"
#include "bingo_descriptions.h"
#include "bingo_objective_info.h"
#include "engine/rand.h"

void setup_bingo_objectives(u32 seed);

#ifndef GEN_VERSION
#define GEN_VERSION "dev"
#endif

// ---------------------------------------------------------------------------
// Fresh-boot weight tables, same trick as test/host/test_bingo.c: the board
// setup mutates its "uses remaining" counters, so save the pristine tables
// once and put them back before every board.

struct ObjectiveWeight {
    s32 objective;
    s32 weight;
    s32 usesRemaining;
};
extern struct ObjectiveWeight sWeightsEasy[], sWeightsMedium[], sWeightsHard[], sWeightsCenter[];
extern s32 sWeightsSizeEasy, sWeightsSizeMedium, sWeightsSizeHard, sWeightsSizeCenter;

#define MAX_WEIGHTS 64
static struct ObjectiveWeight sSavedEasy[MAX_WEIGHTS], sSavedMedium[MAX_WEIGHTS],
                              sSavedHard[MAX_WEIGHTS], sSavedCenter[MAX_WEIGHTS];
static int sWeightsSaved = 0;

static void save_or_restore_weights(void) {
    if (!sWeightsSaved) {
        memcpy(sSavedEasy, sWeightsEasy, sWeightsSizeEasy * sizeof(struct ObjectiveWeight));
        memcpy(sSavedMedium, sWeightsMedium, sWeightsSizeMedium * sizeof(struct ObjectiveWeight));
        memcpy(sSavedHard, sWeightsHard, sWeightsSizeHard * sizeof(struct ObjectiveWeight));
        memcpy(sSavedCenter, sWeightsCenter, sWeightsSizeCenter * sizeof(struct ObjectiveWeight));
        sWeightsSaved = 1;
    } else {
        memcpy(sWeightsEasy, sSavedEasy, sWeightsSizeEasy * sizeof(struct ObjectiveWeight));
        memcpy(sWeightsMedium, sSavedMedium, sWeightsSizeMedium * sizeof(struct ObjectiveWeight));
        memcpy(sWeightsHard, sSavedHard, sWeightsSizeHard * sizeof(struct ObjectiveWeight));
        memcpy(sWeightsCenter, sSavedCenter, sWeightsSizeCenter * sizeof(struct ObjectiveWeight));
    }
}

// The public API still speaks "target" (1, 2, 3, or 12 for blackout), the
// options the site offers; the game replaced gbBingoTarget with the
// BingoGameMode enum. Board bytes are mode-independent either way.
static enum BingoGameMode mode_from_target(s32 target) {
    switch (target) {
        case 2:  return BINGO_MODE_LINE_2;
        case 3:  return BINGO_MODE_LINE_3;
        case 12: return BINGO_MODE_BLACKOUT;
        default: return BINGO_MODE_LINE_1;
    }
}

static void generate(u32 seed, s32 target, const u8 *disabled) {
    int i;
    save_or_restore_weights();
    memset(gBingoObjectives, 0, sizeof(gBingoObjectives));
    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
        gBingoObjectivesDisabled[i] = disabled != NULL ? disabled[i] : 0;
    }
    gbBingoMode = mode_from_target(target);
    gbBingosCompleted = 0;
    setup_bingo_objectives(seed);
}

// ---------------------------------------------------------------------------
// Output buffer with JSON helpers.

static char sBuf[131072];
static size_t sLen;

static void emitf(const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(sBuf + sLen, sizeof(sBuf) - sLen, fmt, ap);
    va_end(ap);
    if (n > 0 && sLen + n < sizeof(sBuf)) {
        sLen += n;
    }
}

// Bytes outside printable ASCII become \u00xx escapes, so in-game glyph
// bytes (like the 0xFA filled star in titles) survive the trip and the JS
// side decides how to render them.
static void emit_json_str(const char *key, const char *s) {
    const unsigned char *p;
    emitf("\"%s\":\"", key);
    for (p = (const unsigned char *) s; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            emitf("\\%c", *p);
        } else if (*p >= 0x20 && *p < 0x7F) {
            emitf("%c", *p);
        } else {
            emitf("\\u%04x", *p);
        }
    }
    emitf("\"");
}

// ---------------------------------------------------------------------------
// Exports.

EMSCRIPTEN_KEEPALIVE
s32 gen_num_objective_types(void) {
    return BINGO_OBJECTIVE_TOTAL_AMOUNT;
}

EMSCRIPTEN_KEEPALIVE
const char *gen_version(void) {
    return GEN_VERSION;
}

// One entry per objective type, for building the options screen.
EMSCRIPTEN_KEEPALIVE
const char *gen_options_json(void) {
    enum BingoObjectiveType t;
    char label[64];
    int first = 1;

    sLen = 0;
    emitf("[");
    for (t = BINGO_OBJECTIVE_TYPE_MIN; t < BINGO_OBJECTIVE_TOTAL_AMOUNT; t++) {
        struct BingoObjectiveInfo *info = get_objective_info(t);
        if (info == NULL) {
            continue;
        }
        reverse_encode_str(info->optionText, label);
        emitf(first ? "{" : ",{");
        first = 0;
        emitf("\"type\":%d,\"icon\":%d,", info->type, info->icon);
        emit_json_str("label", label);
        emitf("}");
    }
    emitf("]");
    return sBuf;
}

// Objective presets, mirroring the game's Preset row. The web generator
// has no unlock or game-mode concepts, so only the objective mask crosses
// over, as a list of disabled type numbers. Names must stay plain ASCII
// (the game's labels live in the menu charmap); keep in sync with
// enum BingoPresetId.
static const char *sPresetNames[BINGO_PRESET_COUNT] = { "SRL", "Vanilla", "Casual" };

EMSCRIPTEN_KEEPALIVE
const char *gen_presets_json(void) {
    s32 p, t;
    int first;

    sLen = 0;
    emitf("[");
    for (p = 0; p < BINGO_PRESET_COUNT; p++) {
        emitf(p == 0 ? "{" : ",{");
        emit_json_str("name", sPresetNames[p]);
        emitf(",\"off\":[");
        first = 1;
        for (t = 0; t < BINGO_OBJECTIVE_TOTAL_AMOUNT; t++) {
            if ((gBingoPresets[p].objectivesDisabled >> t) & 1) {
                emitf(first ? "%d" : ",%d", t);
                first = 0;
            }
        }
        emitf("]}");
    }
    emitf("]");
    return sBuf;
}

// Raw 16x16 RGBA16 texture bytes (512 of them) for a board icon.
EMSCRIPTEN_KEEPALIVE
const u8 *gen_icon_rgba16(s32 icon) {
    struct BingoObjectiveInfo *info = get_objective_info_from_icon(icon);
    return info != NULL ? info->texture : NULL;
}

static void emit_cell_json(int i) {
    struct BingoObjective *o = &gBingoObjectives[i];
    char text[600];

    emitf("{\"type\":%d,\"class\":%d,\"icon\":%d,", o->type, o->class, o->icon);
    emit_json_str("title", o->title);
    emitf(",");
    describe_objective(o, text);
    emit_json_str("desc", text);

    switch (o->type) {
        case BINGO_OBJECTIVE_STAR:
        case BINGO_OBJECTIVE_STAR_TTC_RANDOM:
        case BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK:
        case BINGO_OBJECTIVE_STAR_GREEN_DEMON:
        case BINGO_OBJECTIVE_STAR_DAREDEVIL:
            emitf(",\"course\":%d,\"star\":%d",
                  o->data.starObjective.course, o->data.starObjective.starIndex);
            break;
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
            emitf(",\"course\":%d,\"star\":%d,",
                  o->data.abcStarObjective.course, o->data.abcStarObjective.starIndex);
            emit_json_str("hint", o->data.abcStarObjective.hint ? o->data.abcStarObjective.hint : "");
            break;
        case BINGO_OBJECTIVE_STAR_TIMED:
            emitf(",\"course\":%d,\"star\":%d,\"maxTime\":%d",
                  o->data.starTimerObjective.course, o->data.starTimerObjective.starIndex,
                  o->data.starTimerObjective.maxTime);
            break;
        case BINGO_OBJECTIVE_STAR_CLICK_GAME:
            emitf(",\"course\":%d,\"star\":%d,\"maxClicks\":%d",
                  o->data.starClicksObjective.course, o->data.starClicksObjective.starIndex,
                  o->data.starClicksObjective.maxClicks);
            break;
        case BINGO_OBJECTIVE_COIN:
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
        case BINGO_OBJECTIVE_RANDOM_RED_COINS:
        case BINGO_OBJECTIVE_SPLATOON:
            emitf(",\"course\":%d,\"toGet\":%d",
                  o->data.courseCollectableData.course, o->data.courseCollectableData.toGet);
            break;
        case BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS:
        case BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS:
            emitf(",\"toGetTotal\":%d,\"toGetEachCourse\":%d",
                  o->data.multiCourseCollectableData.toGetTotal,
                  o->data.multiCourseCollectableData.toGetEachCourse);
            break;
        case BINGO_OBJECTIVE_BOWSER:
            emitf(",\"level\":%d", o->data.levelData.level);
            break;
        default:
            emitf(",\"toGet\":%d", o->data.collectableData.toGet);
            break;
    }
    emitf("}");
}

// The 25 cells in the same order the game draws them: cell (row i, col j)
// is gBingoObjectives[5 * i + j].
EMSCRIPTEN_KEEPALIVE
const char *gen_board_json(u32 seed, s32 target, const u8 *disabled) {
    int i;

    generate(seed, target, disabled);

    sLen = 0;
    emitf("{\"seed\":%u,\"target\":%d,\"version\":\"%s\",\"cells\":[", seed, target, GEN_VERSION);
    for (i = 0; i < 25; i++) {
        if (i > 0) {
            emitf(",");
        }
        emit_cell_json(i);
    }
    emitf("]}");
    return sBuf;
}

// The exact golden-file format from test/host/test_bingo.c (dump_cell), so
// the checker can byte-compare this output against the goldens and against
// the gcc-built oracle. Keep in sync with dump_cell there.
static void emit_cell_dump(int i) {
    struct BingoObjective *o = &gBingoObjectives[i];

    emitf("%02d type=%02d class=%d icon=%02d title=\"%s\"",
          i, o->type, o->class, o->icon, o->title);

    switch (o->type) {
        case BINGO_OBJECTIVE_STAR:
        case BINGO_OBJECTIVE_STAR_TTC_RANDOM:
        case BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK:
        case BINGO_OBJECTIVE_STAR_GREEN_DEMON:
        case BINGO_OBJECTIVE_STAR_DAREDEVIL:
            emitf(" course=%d star=%d",
                  o->data.starObjective.course, o->data.starObjective.starIndex);
            break;
        case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
        case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
            emitf(" course=%d star=%d hint=\"%s\"",
                  o->data.abcStarObjective.course, o->data.abcStarObjective.starIndex,
                  o->data.abcStarObjective.hint ? o->data.abcStarObjective.hint : "");
            break;
        case BINGO_OBJECTIVE_STAR_TIMED:
            emitf(" course=%d star=%d maxTime=%d",
                  o->data.starTimerObjective.course, o->data.starTimerObjective.starIndex,
                  o->data.starTimerObjective.maxTime);
            break;
        case BINGO_OBJECTIVE_STAR_CLICK_GAME:
            emitf(" course=%d star=%d maxClicks=%d",
                  o->data.starClicksObjective.course, o->data.starClicksObjective.starIndex,
                  o->data.starClicksObjective.maxClicks);
            break;
        case BINGO_OBJECTIVE_COIN:
        case BINGO_OBJECTIVE_1UPS_IN_LEVEL:
        case BINGO_OBJECTIVE_STARS_IN_LEVEL:
        case BINGO_OBJECTIVE_RANDOM_RED_COINS:
        case BINGO_OBJECTIVE_SPLATOON:
            emitf(" course=%d toGet=%d",
                  o->data.courseCollectableData.course, o->data.courseCollectableData.toGet);
            break;
        case BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS:
        case BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS:
            emitf(" toGetTotal=%d toGetEachCourse=%d",
                  o->data.multiCourseCollectableData.toGetTotal,
                  o->data.multiCourseCollectableData.toGetEachCourse);
            break;
        case BINGO_OBJECTIVE_BOWSER:
            emitf(" level=%d", o->data.levelData.level);
            break;
        default:
            emitf(" toGet=%d", o->data.collectableData.toGet);
            break;
    }
    emitf("\n");
}

EMSCRIPTEN_KEEPALIVE
const char *gen_board_dump(u32 seed, s32 target, const u8 *disabled) {
    int i;

    generate(seed, target, disabled);

    sLen = 0;
    for (i = 0; i < 25; i++) {
        emit_cell_dump(i);
    }
    return sBuf;
}
