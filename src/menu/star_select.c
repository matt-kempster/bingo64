#include <PR/ultratypes.h>

#include "audio/external.h"
#include "behavior_data.h"
#include "engine/behavior_script.h"
#include "engine/graph_node.h"
#include "eu_translation.h"
#include "game/area.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/level_update.h"
#include "game/memory.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/rumble_init.h"
#include "game/save_file.h"
#include "game/segment2.h"
#include "game/segment7.h"
#include "sm64.h"
#include "star_select.h"
#include "game/bingo.h"
#include "game/bingo_tracking_star.h"
#include "game/camera.h"
#include "game/splatoon.h"
#include "text_strings.h"
#include "game/bingo_ui.h"
#include "extras/draw_util.h"
#ifndef TARGET_N64
#include <stdlib.h>
#endif

/**
 * @file star_select.c
 * This file implements how the star select screen (act selector) function.
 * That includes handles what stars can be selected, star selector types,
 * strings, act values, and star selector model rendering if a star is collected or not.
 */

static struct Object *sStarSelectorModels[9];

// The act the course is loaded as, affects whether some objects spawn.
static s8 sLoadedActNum;

// Number of obtained stars, excluding the coin star.
static u8 sObtainedStars;

// Total number of stars that appear in the act selector menu.
static s8 sVisibleStars;

// Act selected when the act menu is first opened.
static u8 sInitSelectedActNum;

// Index value of the act selected in the act menu.
static s8 sSelectedActIndex = 0;

// Index value of the star that is selectable in the act menu.
// Excluding the next star, it doesn't count other transparent stars.
static s8 sSelectableStarIndex = 0;

// Act Selector menu timer that keeps counting until you choose an act.
static s32 sActSelectorMenuTimer = 0;

/**
 * Act Selector Star Type Loop Action
 * Defines a select type for a star in the act selector.
 */
void bhv_act_selector_star_type_loop(void) {
    switch (gCurrentObject->oStarSelectorType) {
        // If a star is not selected, don't rotate or change size
        case STAR_SELECTOR_NOT_SELECTED:
            gCurrentObject->oStarSelectorSize -= 0.1;
            if (gCurrentObject->oStarSelectorSize < 1.0) {
                gCurrentObject->oStarSelectorSize = 1.0;
            }
            gCurrentObject->oFaceAngleYaw = 0;
            break;
        // If a star is selected, rotate and slightly increase size
        case STAR_SELECTOR_SELECTED:
            gCurrentObject->oStarSelectorSize += 0.1;
            if (gCurrentObject->oStarSelectorSize > 1.3) {
                gCurrentObject->oStarSelectorSize = 1.3;
            }
            gCurrentObject->oFaceAngleYaw += 0x800;
            break;
        // If the 100 coin star is selected, rotate
        case STAR_SELECTOR_100_COINS:
            gCurrentObject->oFaceAngleYaw += 0x800;
            break;
    }
    // Scale act selector stars depending of the type selected
    cur_obj_scale(gCurrentObject->oStarSelectorSize);
    // Unused timer, only referenced here. Probably replaced by sActSelectorMenuTimer
    gCurrentObject->oStarSelectorTimer++;
}

void bhv_act_selector_star_type_reversed_loop(void) {
    switch (gCurrentObject->oStarSelectorType) {
        // If a star is not selected, don't rotate or change size
        case STAR_SELECTOR_NOT_SELECTED:
            gCurrentObject->oStarSelectorSize -= 0.1;
            if (gCurrentObject->oStarSelectorSize < 1.0) {
                gCurrentObject->oStarSelectorSize = 1.0;
            }
            gCurrentObject->oFaceAngleYaw = 0;
            break;
        // If a star is selected, rotate and slightly increase size
        case STAR_SELECTOR_SELECTED:
            gCurrentObject->oStarSelectorSize += 0.1;
            if (gCurrentObject->oStarSelectorSize > 1.3) {
                gCurrentObject->oStarSelectorSize = 1.3;
            }
            gCurrentObject->oFaceAngleYaw -= 0x800;
            break;
        // If the 100 coin star is selected, rotate
        case STAR_SELECTOR_100_COINS:
            gCurrentObject->oFaceAngleYaw -= 0x800;
            break;
    }
    // Scale act selector stars depending of the type selected
    cur_obj_scale(gCurrentObject->oStarSelectorSize);
    // Unused timer, only referenced here. Probably replaced by sActSelectorMenuTimer
    gCurrentObject->oStarSelectorTimer++;
}

/**
 * Renders the 100 coin star with an special star selector type.
 */
void render_100_coin_star(u8 stars) {
    if (stars & (1 << 6)) {
        // If the 100 coin star has been collected, create a new star selector next to the coin score.
        sStarSelectorModels[6] = spawn_object_abs_with_rot(gCurrentObject, 0, MODEL_STAR,
                                                        bhvActSelectorStarType, 370, 24, -300, 0, 0, 0);
        sStarSelectorModels[6]->oStarSelectorSize = 0.8;
        sStarSelectorModels[6]->oStarSelectorType = STAR_SELECTOR_100_COINS;
    }
}

// ---------------------------------------------------------------------------
// Bingo modifier picker (prototype). The modifier is its own axis: up/down
// (Z/R still work) picks it, left/right picks the act as in vanilla. Drawn
// in 2D with the board's own icons. Small dots mark what the board wants in
// this course: acts some tile needs, modifiers some tile needs; one line
// confirms when the current act + modifier is itself a board tile.

// Icon per enum BingoModifier (the icon its board tiles use).
static const u8 sBingoModifierIcons[BINGO_STARS_TOTAL_AMOUNT] = {
    BINGO_ICON_STAR,                  // NONE
    BINGO_ICON_STAR_GREEN_DEMON,      // GREEN_DEMON
    BINGO_ICON_STAR_REVERSE_JOYSTICK, // REVERSE_JOYSTICK
    BINGO_ICON_RANDOM_RED_COINS,      // ORDERED_RED_COINS
    BINGO_ICON_STAR_CLICK_GAME,       // CLICK_GAME
    BINGO_ICON_RANDOM_STARS,          // RANDOM_STARS
    BINGO_ICON_STAR_DAREDEVIL,        // DAREDEVIL
    BINGO_ICON_SPLATOON,              // SPLATOON
};

// What the board wants in this course, rebuilt every frame (claims can
// land mid-screen online). sWantPair[act][mod]: act 0 = any act.
static u8 sWantPair[8][BINGO_STARS_TOTAL_AMOUNT];
static u8 sWantAct[8];
static u8 sWantMod[BINGO_STARS_TOTAL_AMOUNT];

// 'a' = strip where the old 3D stars were, 'b' = column down the left.
static char sBingoModUi = 'a';
static s32 sBingoModStickHeld = 0;

static void bingo_want_pair(s32 act, s32 mod) {
    sWantPair[act][mod] = 1;
    if (act != 0) {
        sWantAct[act] = 1;
    }
    if (mod != BINGO_MODIFIER_NONE) {
        sWantMod[mod] = 1;
    }
}

static void bingo_compute_wants(void) {
    s32 i, j;
    for (i = 0; i < 8; i++) {
        sWantAct[i] = 0;
        for (j = 0; j < BINGO_STARS_TOTAL_AMOUNT; j++) {
            sWantPair[i][j] = 0;
        }
    }
    for (j = 0; j < BINGO_STARS_TOTAL_AMOUNT; j++) {
        sWantMod[j] = 0;
    }
    if (!COURSE_IS_MAIN_COURSE(gCurrCourseNum)) {
        return;
    }
    for (i = 0; i < 25; i++) {
        struct BingoObjective *o = &gBingoObjectives[i];
        s32 mod = -1;
        s32 act;
        if (o->state == BINGO_STATE_COMPLETE) {
            continue;
        }
        if (gbBingoMode == BINGO_MODE_LOCKOUT && gBingoCellClaimers[i] != 0) {
            continue;
        }
        switch (o->type) {
            case BINGO_OBJECTIVE_STAR:
            case BINGO_OBJECTIVE_STAR_TIMED:
            case BINGO_OBJECTIVE_STAR_TTC_RANDOM:
            case BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE:
            case BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE:
            case BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE:
            case BINGO_OBJECTIVE_STAR_COINLESS:
                mod = BINGO_MODIFIER_NONE;
                break;
            case BINGO_OBJECTIVE_STAR_GREEN_DEMON:
                mod = BINGO_MODIFIER_GREEN_DEMON;
                break;
            case BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK:
                mod = BINGO_MODIFIER_REVERSE_JOYSTICK;
                break;
            case BINGO_OBJECTIVE_STAR_CLICK_GAME:
                mod = BINGO_MODIFIER_CLICK_GAME;
                break;
            case BINGO_OBJECTIVE_STAR_DAREDEVIL:
                mod = BINGO_MODIFIER_DAREDEVIL;
                break;
            case BINGO_OBJECTIVE_RANDOM_STARS:
                if ((s32) o->data.courseCollectableData.course == gCurrCourseNum) {
                    bingo_want_pair(0, BINGO_MODIFIER_RANDOM_STARS);
                }
                continue;
            case BINGO_OBJECTIVE_RANDOM_RED_COINS:
                if ((s32) o->data.courseCollectableData.course == gCurrCourseNum) {
                    bingo_want_pair(0, BINGO_MODIFIER_ORDERED_RED_COINS);
                }
                continue;
            case BINGO_OBJECTIVE_SPLATOON:
                if ((s32) o->data.courseCollectableData.course == gCurrCourseNum) {
                    bingo_want_pair(0, BINGO_MODIFIER_SPLATOON);
                }
                continue;
            default:
                continue;
        }
        // Single-star tiles share the (course, starIndex) prefix. The
        // 100-coin star (index 6) exists in every act.
        if ((s32) o->data.starObjective.course != gCurrCourseNum) {
            continue;
        }
        act = o->data.starObjective.starIndex < 6 ? o->data.starObjective.starIndex + 1 : 0;
        bingo_want_pair(act, mod);
    }
}

// Up/down (stick or D-pad) and the old Z/R aliases, with hold-to-repeat.
static s32 bingo_modifier_input(void) {
    s32 dir = 0;
    u16 pressed = gPlayer1Controller->buttonPressed;
    s32 stick = gPlayer1Controller->rawStickY > 60 ? -1 : gPlayer1Controller->rawStickY < -60 ? 1 : 0;
    if (pressed & (U_JPAD | Z_TRIG)) {
        dir = -1;
    } else if (pressed & (D_JPAD | R_TRIG)) {
        dir = 1;
    } else if (stick != 0) {
        sBingoModStickHeld++;
        if (sBingoModStickHeld == 1 || (sBingoModStickHeld > 12 && sBingoModStickHeld % 4 == 0)) {
            dir = stick;
        }
    }
    if (stick == 0) {
        sBingoModStickHeld = 0;
    }
    return dir;
}

void render_bingo_modifier_star(void) {
#ifndef TARGET_N64
    if (getenv("BINGO64_MODUI") != NULL) {
        sBingoModUi = getenv("BINGO64_MODUI")[0];
    }
#endif
    sBingoModStickHeld = 0;
    // Restore the modifier last confirmed for this course, if any.
    if (COURSE_IS_MAIN_COURSE(gCurrCourseNum)) {
        gBingoStarSelected = gBingoStickyModifier[gCurrCourseNum - 1];
    } else {
        gBingoStarSelected = BINGO_MODIFIER_NONE;
    }
}

/**
 * Act Selector Init Action
 * Checks how many stars has been obtained in a course, to render
 * the correct star models, the 100 coin star and also handles
 * checks of what star should be next in sInitSelectedActNum.
 */
void bhv_act_selector_init(void) {
    s16 i = 0;
    s32 selectorModelIDs[10];
    u8 stars = save_file_get_star_flags(gCurrSaveFileNum - 1, COURSE_NUM_TO_INDEX(gCurrCourseNum));

    gStarSelectScreenActive = 1;

    sVisibleStars = 0;
    if (gCurrCourseNum > COURSE_STAGES_MAX) {
        sObtainedStars = 0;  // yeah todo
    }
    while (i != sObtainedStars) {
        if (stars & (1 << sVisibleStars)) // Star has been collected
        {
            // has it *actually* been collected tho?
            if (bingo_get_course_flags(gCurrCourseNum - 1) & (1 << sVisibleStars)) {
                // yes
                selectorModelIDs[sVisibleStars] = MODEL_STAR;
            } else {
                // no
                selectorModelIDs[sVisibleStars] = MODEL_TRANSPARENT_STAR;
            }
            i++;
        }
        else if (!gBingoFullGameUnlocked) // Star has not been collected
        {
            selectorModelIDs[sVisibleStars] = MODEL_TRANSPARENT_STAR;
            if (sInitSelectedActNum  == 0) // If this is the first star that has not been collected,
            // set the default selection to this star.
            {
                sInitSelectedActNum = sVisibleStars + 1;
                sSelectableStarIndex = sVisibleStars;
            }
        }
        sVisibleStars++;
    }

    // If the stars have been collected in order so far, show the next star.
    if (sVisibleStars == sObtainedStars && sVisibleStars != 6) {
        selectorModelIDs[sVisibleStars] = MODEL_TRANSPARENT_STAR;
        sInitSelectedActNum = sVisibleStars + 1;
        sSelectableStarIndex = sVisibleStars;
        sVisibleStars++;
    }

    // If all stars have been collected, set the default selection to the last star.
    if (sObtainedStars == 6) {
        sInitSelectedActNum = sVisibleStars;
    }

    //! Useless, since sInitSelectedActNum has already been set in this
    //! scenario by the code that shows the next uncollected star.
    if (sObtainedStars == 0) {
        sInitSelectedActNum = 1;
    }

    // Restore the act last confirmed for this course, if it is still
    // selectable. sSelectableStarIndex counts selectable stars only, so
    // mirror the selectability check from bhv_act_selector_loop.
    if (COURSE_IS_MAIN_COURSE(gCurrCourseNum) && gBingoStickyActNum[gCurrCourseNum - 1] != 0) {
        s8 stickyIndex = gBingoStickyActNum[gCurrCourseNum - 1] - 1;
        if (stickyIndex < sVisibleStars
            && ((stars & (1 << stickyIndex)) || stickyIndex + 1 == sInitSelectedActNum)) {
            sSelectableStarIndex = 0;
            for (i = 0; i < stickyIndex; i++) {
                if ((stars & (1 << i)) || i + 1 == sInitSelectedActNum) {
                    sSelectableStarIndex++;
                }
            }
        }
    }

    // Render star selector objects
    if (gCurrCourseNum > COURSE_STAGES_MAX) {
        sVisibleStars = 1;  // yeah todo
    }

    for (i = 0; i < sVisibleStars; i++) {
        sStarSelectorModels[i] =
            spawn_object_abs_with_rot(gCurrentObject, 0, selectorModelIDs[i], bhvActSelectorStarType,
                                      75 + sVisibleStars * -75 + i * 152, 340, -300, 0, 0, 0);
        sStarSelectorModels[i]->oStarSelectorSize = 1.0f;
    }

    // actually look up the 100 coin star's collection status :)
    render_100_coin_star(bingo_get_course_flags(gCurrCourseNum - 1));

    render_bingo_modifier_star();
}

/**
 * Act Selector Loop Action
 * Handles star selector scrolling depending of what stars are
 * selectable, whenever all 6 stars are obtained or not.
 * Also handles 2 star selector types whenever the star is selected
 * or not, the types are defined in bhv_act_selector_star_type_loop.
 */
void bhv_act_selector_loop(void) {
    s8 i;
    u8 starIndexCounter;
    u8 stars = save_file_get_star_flags(gCurrSaveFileNum - 1, COURSE_NUM_TO_INDEX(gCurrCourseNum));

    if (sObtainedStars != 6) {
        // Sometimes, stars are not selectable even if they appear on the screen.
        // This code filters selectable and non-selectable stars.
        sSelectedActIndex = 0;
        handle_menu_scrolling(MENU_SCROLL_HORIZONTAL, &sSelectableStarIndex, 0, sObtainedStars);
        starIndexCounter = sSelectableStarIndex;
        for (i = 0; i < sVisibleStars; i++) {
            // Can the star be selected (is it either already completed or the first non-completed mission)
            if ((stars & (1 << i)) || i == sInitSelectedActNum - 1) {
                if (starIndexCounter == 0) { // We have reached the sSelectableStarIndex-th selectable star.
                    sSelectedActIndex = i;
                    break;
                }
                starIndexCounter--;
            }
        }
    } else {
        // If all stars are collected then they are all selectable.
        handle_menu_scrolling(MENU_SCROLL_HORIZONTAL, &sSelectableStarIndex, 0, sVisibleStars - 1);
        sSelectedActIndex = sSelectableStarIndex;
    }

    // Star selector type handler
    for (i = 0; i < sVisibleStars; i++) {
        if (sSelectedActIndex == i) {
            sStarSelectorModels[i]->oStarSelectorType = STAR_SELECTOR_SELECTED;
        } else {
            sStarSelectorModels[i]->oStarSelectorType = STAR_SELECTOR_NOT_SELECTED;
        }
    }

    // Bingo modifier: its own axis on up/down.
    {
        s32 dir = bingo_modifier_input();
        if (dir != 0) {
            play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
            gBingoStarSelected = (gBingoStarSelected + dir + BINGO_STARS_TOTAL_AMOUNT)
                                 % BINGO_STARS_TOTAL_AMOUNT;
        }
    }

    if (gBingoStarSelected == BINGO_MODIFIER_REVERSE_JOYSTICK) {
        gBingoReverseJoystickActive = 1;
    } else {
        gBingoReverseJoystickActive = 0;
    }
    if (gBingoStarSelected == BINGO_MODIFIER_CLICK_GAME) {
        gBingoClickGameActive = 1;
    } else {
        gBingoClickGameActive = 0;
    }
    if (gBingoStarSelected == BINGO_MODIFIER_RANDOM_STARS) {
        gBingoRandomStarsActive = 1;
    } else {
        gBingoRandomStarsActive = 0;
    }
    if (gBingoStarSelected == BINGO_MODIFIER_DAREDEVIL) {
        gBingoDaredevilPrevHealth = gMarioState->health;
        gBingoDaredevilActive = 1;
    } else {
        gBingoDaredevilActive = 0;
    }
    if (gBingoStarSelected == BINGO_MODIFIER_SPLATOON) {
        gSplatoonEnabled = 1;
    } else {
        gSplatoonEnabled = 0;
    }
}

/**
 * Print the course number selected with the wood rgba16 course texture.
 */
#ifdef VERSION_EU
void print_course_number(s16 language) {
#else
void print_course_number(void) {
#endif
    u8 courseNum[4];

    create_dl_translation_matrix(MENU_MTX_PUSH, 158.0f, 63.0f, 0.0f);

    // Full wood texture in JP & US, lower part of it on EU
    gSPDisplayList(gDisplayListHead++, dl_menu_rgba16_wood_course);

#ifdef VERSION_EU
    // Change upper part of the wood texture depending of the language defined
    switch (language) {
        case LANGUAGE_ENGLISH:
            gSPDisplayList(gDisplayListHead++, dl_menu_texture_course_upper);
            break;
        case LANGUAGE_FRENCH:
            gSPDisplayList(gDisplayListHead++, dl_menu_texture_niveau_upper);
            break;
        case LANGUAGE_GERMAN:
            gSPDisplayList(gDisplayListHead++, dl_menu_texture_kurs_upper);
            break;
    }

    gSPDisplayList(gDisplayListHead++, dl_menu_rgba16_wood_course_end);
#endif

    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);

    int_to_str(gCurrCourseNum, courseNum);

    #define Y_POS 174
    if (gCurrCourseNum < 10) { // 1 digit number
        print_hud_lut_string(HUD_LUT_GLOBAL, 152, Y_POS, courseNum);
    } else { // 2 digit number
        print_hud_lut_string(HUD_LUT_GLOBAL, 143, Y_POS, courseNum);
    }
    #undef Y_POS

    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_end);
}

#ifdef VERSION_JP
#define ACT_NAME_X 158
#else
#define ACT_NAME_X 163
#endif

u8 gBingoTextNoModifier[] = { BINGO_NO_MODIFIER };
u8 gBingoTextOnYourBoard[] = { BINGO_ON_YOUR_BOARD };
u8 gBingoTextGreenDemon[] = { BINGO_GREEN_DEMON };
u8 gBingoTextReverseJoystick[] = { BINGO_REVERSE_JOYSTICK };
u8 gBingoTextClickGame[] = { BINGO_CLICK_GAME };
u8 gBingoTextDaredevil[] = { BINGO_DAREDEVIL_1HP };
u8 gBingoTextRandomRedCoins[] = { BINGO_RANDOM_ROUTE_RED_COINS };
u8 gBingoTextSplatoon[] = { BINGO_SPLATOON };
u8 gBingoTextRandomStars[] = { BINGO_RANDOM_STARS };

// Screen coords are top-down here (quads); print_bingo_icon takes 224 - top.
#define MOD_DOT(x, y) print_solid_color_quad((x), (y), (x) + 3, (y) + 3, 230, 70, 30, 255)

static void print_bingo_modifier_picker(u8 *name) {
    s32 i, x, y;
    s32 act = sSelectedActIndex + 1;
    s32 mod = gBingoStarSelected;
    s32 onBoard;

    bingo_compute_wants();
    onBoard = sWantPair[act][mod] || sWantPair[0][mod];

    // Dots next to the act numbers the board wants.
    for (i = 1; i <= sVisibleStars && i < 8; i++) {
        if (sWantAct[i] || sWantPair[0][BINGO_MODIFIER_NONE]) {
            MOD_DOT(i * 34 - sVisibleStars * 17 + 139 + 10, 18);
        }
    }

    for (i = 0; i < BINGO_STARS_TOTAL_AMOUNT; i++) {
        if (sBingoModUi == 'b') {
            x = 14;
            y = 34 + i * 20;
        } else {
            x = 160 - (BINGO_STARS_TOTAL_AMOUNT * 22 - 6) / 2 + i * 22;
            y = 84;
        }
        if (i == mod) {
            print_solid_color_quad(x - 3, y - 3, x + 19, y + 19, 0, 0, 0, 45);
        }
        if (sWantMod[i]) {
            if (sBingoModUi == 'b') {
                MOD_DOT(x + 21, y + 6);
            } else {
                MOD_DOT(x + 6, y + 20);
            }
        }
        gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
        print_bingo_icon_alpha(x, 224 - y, sBingoModifierIcons[i], i == mod ? 255 : 120);
        gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
    }

    gSPDisplayList(gDisplayListHead++, dl_menu_ia8_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 0, 0, 0, 255);
    print_menu_generic_string(get_str_x_pos_from_center(159, name, 10.0f), 112, name);
    if (onBoard) {
        gDPSetEnvColor(gDisplayListHead++, 20, 130, 40, 255);
        print_menu_generic_string(get_str_x_pos_from_center(159, gBingoTextOnYourBoard, 10.0f), 126,
                                  gBingoTextOnYourBoard);
    }
    gSPDisplayList(gDisplayListHead++, dl_menu_ia8_text_end);
}

/**
 * Print act selector strings, some with special checks.
 */
void print_act_selector_strings(void) {
#ifdef VERSION_EU
    unsigned char myScore[][10] = { {TEXT_MYSCORE}, {TEXT_MY_SCORE_FR}, {TEXT_MY_SCORE_DE} };
#else
    unsigned char myScore[] = { TEXT_MYSCORE };
#endif

    unsigned char starNumbers[] = { TEXT_ZERO };

#ifdef VERSION_EU
    u8 **levelNameTbl;
    u8 *currLevelName;
    u8 **actNameTbl;
#else
    u8 **levelNameTbl = segmented_to_virtual(seg2_course_name_table);
    u8 *currLevelName = segmented_to_virtual(levelNameTbl[COURSE_NUM_TO_INDEX(gCurrCourseNum)]);
    u8 **actNameTbl = segmented_to_virtual(seg2_act_name_table);
#endif
    u8 *selectedActName;
    s16 lvlNameX;
    s16 actNameX;
    s8 i;
#ifdef VERSION_EU
    s16 language = eu_get_language();
#endif

    u8 *bingoModifierName;

    create_dl_ortho_matrix();

    // No need for coin highscore in bingo.
    // // Print the coin highscore.
    // gSPDisplayList(gDisplayListHead++, dl_rgba16_text_begin);
    // gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
    // print_hud_my_score_coins(1, gCurrSaveFileNum - 1, gCurrCourseNum - 1, 155, 106);
    // gSPDisplayList(gDisplayListHead++, dl_rgba16_text_end);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 0, 0, 0, 255);
    // // Print the "MY SCORE" text if the coin score is more than 0
    // if (save_file_get_course_coin_score(gCurrSaveFileNum - 1, gCurrCourseNum - 1) != 0) {
    //     print_generic_string(102, 118, myScore);
    // }
    // Print the level name; add 3 to skip the number and spacing to get to the actual string to center.
    // TODO: There has to be a way to merge these, but US seems to need lvlNameX and EU doesn't
    // TODO: allow it to be declared.
#ifdef VERSION_EU
    print_generic_string(get_str_x_pos_from_center(160, currLevelName + 3, 10.0f), 33, currLevelName + 3);
#else
    lvlNameX = get_str_x_pos_from_center(160, currLevelName + 3, 10.0f);
    print_generic_string(lvlNameX, 10, currLevelName + 3);
#endif
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);

#ifdef VERSION_EU
    print_course_number(language);
#else
    print_course_number();
#endif

#ifdef VERSION_CN
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
#else
    gSPDisplayList(gDisplayListHead++, dl_menu_ia8_text_begin);
#endif
    gDPSetEnvColor(gDisplayListHead++, 0, 0, 0, 255);
    // Print the name of the selected act.
    if (sVisibleStars != 0) {
        if (gCurrCourseNum <= COURSE_STAGES_MAX) {
            selectedActName = segmented_to_virtual(actNameTbl[(gCurrCourseNum - 1) * 6 + sSelectedActIndex]);
        } else {
            selectedActName = segmented_to_virtual(actNameTbl[(COURSE_STAGES_MAX) * 6]);
            sVisibleStars = 1;
        }
        actNameX = get_str_x_pos_from_center(ACT_NAME_X, selectedActName, 8.0f);
        print_menu_generic_string(actNameX, 63, selectedActName);
    }

#ifdef VERSION_CN
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);

    gSPDisplayList(gDisplayListHead++, dl_menu_ia8_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 0, 0, 0, 255);
#endif

    // Print the numbers above each star.
    for (i = 1; i <= sVisibleStars; i++) {
        starNumbers[0] = i;
        print_menu_generic_string(i * 34 - sVisibleStars * 17 + 139, 15, starNumbers);
    }


    switch (gBingoStarSelected) {
        case BINGO_MODIFIER_NONE:
            bingoModifierName = gBingoTextNoModifier;
            break;
        case BINGO_MODIFIER_GREEN_DEMON:
            bingoModifierName = gBingoTextGreenDemon;
            break;
        case BINGO_MODIFIER_REVERSE_JOYSTICK:
            bingoModifierName = gBingoTextReverseJoystick;
            break;
        case BINGO_MODIFIER_CLICK_GAME:
            bingoModifierName = gBingoTextClickGame;
            break;
        case BINGO_MODIFIER_DAREDEVIL:
            bingoModifierName = gBingoTextDaredevil;
            break;
        case BINGO_MODIFIER_ORDERED_RED_COINS:
            bingoModifierName = gBingoTextRandomRedCoins;
            break;
        case BINGO_MODIFIER_SPLATOON:
            bingoModifierName = gBingoTextSplatoon;
            break;
        case BINGO_MODIFIER_RANDOM_STARS:
            bingoModifierName = gBingoTextRandomStars;
            break;
    }

    gSPDisplayList(gDisplayListHead++, dl_menu_ia8_text_end);
    print_bingo_modifier_picker(bingoModifierName);
}

/**
 * Geo function that Print act selector strings.
 *!@bug: This geo function is missing the third param. Harmless in practice due to o32 convention.
 */
#ifdef AVOID_UB
Gfx *geo_act_selector_strings(s16 callContext, UNUSED struct GraphNode *node, UNUSED void *context)
#else
Gfx *geo_act_selector_strings(s16 callContext, UNUSED struct GraphNode *node)
#endif
{
    if (callContext == GEO_CONTEXT_RENDER) {
#ifdef TARGET_N3DS
        gDPForceFlush(gDisplayListHead++);
        gDPSet2d(gDisplayListHead++, 1);
        gDPSetIod(gDisplayListHead++, iodStarSelect);
#endif
        print_act_selector_strings();
#ifdef TARGET_N3DS
        gDPForceFlush(gDisplayListHead++);
        gDPSet2d(gDisplayListHead++, 0);
#endif
    }
    return NULL;
}

/**
 * Initiates act selector values before entering a main course.
 * Also load how much stars a course has, without counting the 100 coin star.
 */
s32 lvl_init_act_selector_values_and_stars(UNUSED s32 arg, UNUSED s32 unused) {
    u8 stars = save_file_get_star_flags(gCurrSaveFileNum - 1, COURSE_NUM_TO_INDEX(gCurrCourseNum));

    sLoadedActNum = 0;
    sInitSelectedActNum = 0;
    sVisibleStars = 0;
    sActSelectorMenuTimer = 0;
#ifdef NO_SEGMENTED_MEMORY
    sSelectedActIndex = 0;
    sSelectableStarIndex = 0;
#endif
    sObtainedStars =
        save_file_get_course_star_count(gCurrSaveFileNum - 1, COURSE_NUM_TO_INDEX(gCurrCourseNum));

    // Don't count 100 coin star
    if (stars & (1 << 6)) {
        sObtainedStars--;
    }

    //! no return value
#ifdef AVOID_UB
    return 0;
#endif
}

/**
 * Loads act selector button actions with selected act value checks.
 * Also updates objects and returns act number selected after is chosen.
 */
s32 lvl_update_obj_and_load_act_button_actions(UNUSED s32 arg, UNUSED s32 unused) {
    s8 bingoClickGameActivate = 0;
    if (sActSelectorMenuTimer >= 11) {
        // If any of these buttons are pressed, play sound and go to course act
        if ((gPlayer1Controller->buttonPressed & Z_BUTTON_DEF(A_BUTTON | START_BUTTON | B_BUTTON))) {
#ifdef VERSION_JP
            play_sound(SOUND_MENU_STAR_SOUND, gGlobalSoundSource);
#else
            play_sound(SOUND_MENU_STAR_SOUND_LETS_A_GO, gGlobalSoundSource);
#endif
#ifdef RUMBLE_FEEDBACK
            queue_rumble_data(60, 70);
            queue_rumble_decay(1);
#endif
            if (sInitSelectedActNum >= sSelectedActIndex + 1) {
                sLoadedActNum = sSelectedActIndex + 1;
            } else {
                sLoadedActNum = sInitSelectedActNum;
            }
            gDialogCourseActNum = sSelectedActIndex + 1;
            bingoClickGameActivate = 1;
            gStarSelectScreenActive = 0;
            if (COURSE_IS_MAIN_COURSE(gCurrCourseNum)) {
                gBingoStickyModifier[gCurrCourseNum - 1] = gBingoStarSelected;
                gBingoStickyActNum[gCurrCourseNum - 1] = sSelectedActIndex + 1;
            }
        }
    }

    area_update_objects();
    sActSelectorMenuTimer++;
    if (gBingoClickGameActive && bingoClickGameActivate) {
        gBingoClickGamePrevCameraSettings = sSelectionFlags;
        gBingoClickGamePrevCameraIndex = gDialogCameraAngleIndex;
        gDialogCameraAngleIndex = CAM_SELECTION_FIXED;
        sSelectionFlags &= ~CAM_MODE_MARIO_SELECTED;
    }
    return sLoadedActNum;
}
