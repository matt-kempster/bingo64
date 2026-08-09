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
#include "game/save_file.h"
#include "game/segment2.h"
#include "game/segment7.h"
#include "game/rumble_init.h"
#include "sm64.h"
#include "star_select.h"
#include "game/bingo.h"
#include "game/bingo_tracking_star.h"
#include "game/camera.h"
#include "game/splatoon.h"
#include "text_strings.h"

/**
 * @file star_select.c
 * This file implements how the star select screen (act selector) function.
 * That includes handles what stars can be selected, star selector types,
 * strings, act values, and star selector model rendering if a star is collected or not.
 */

static struct Object *sStarSelectorModels[9];

// Bingo star selection:
static struct Object *sBingoStarSelectorModels[BINGO_STARS_TOTAL_AMOUNT];

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

static s32 gBingoModifierScrollLockoutTimer = 0;

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
    obj_scale(gCurrentObject->oStarSelectorSize);
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

/**
 * Renders the extra Bingo "modifier" star.
 */
void render_bingo_modifier_star(void) {
    // Indexed by enum BingoModifier.
    static const u32 sBingoStarModels[BINGO_STARS_TOTAL_AMOUNT] = {
        MODEL_STAR,        // BINGO_MODIFIER_NONE
        MODEL_STAR_GREEN,  // BINGO_MODIFIER_GREEN_DEMON
        MODEL_STAR_BLUE,   // BINGO_MODIFIER_REVERSE_JOYSTICK
        MODEL_STAR_ORANGE, // BINGO_MODIFIER_ORDERED_RED_COINS
        MODEL_STAR_GRAY,   // BINGO_MODIFIER_CLICK_GAME
        MODEL_STAR_RED,    // BINGO_MODIFIER_DAREDEVIL
        MODEL_STAR_PINK,   // BINGO_MODIFIER_SPLATOON
    };
    s32 i;
    // The row keeps a fixed total width and gets denser as modifiers are
    // added, so it stays centered without manual position tuning.
    #define ROW_WIDTH 750
    s32 xSpace = ROW_WIDTH / (BINGO_STARS_TOTAL_AMOUNT - 1);
    s32 xLeft = -(xSpace * (BINGO_STARS_TOTAL_AMOUNT - 1)) / 2;

    // Restore the modifier last confirmed for this course, if any.
    if (COURSE_IS_MAIN_COURSE(gCurrCourseNum)) {
        gBingoStarSelected = gBingoStickyModifier[gCurrCourseNum - 1];
    } else {
        gBingoStarSelected = BINGO_MODIFIER_NONE;
    }

    for (i = 0; i < BINGO_STARS_TOTAL_AMOUNT; i++) {
        sBingoStarSelectorModels[i] = spawn_object_abs_with_rot(
            gCurrentObject, 0, sBingoStarModels[i],
            i == BINGO_MODIFIER_REVERSE_JOYSTICK ? bhvActSelectorStarTypeReversed
                                                 : bhvActSelectorStarType,
            xLeft + xSpace * i, 120, -300, 0, 0, 0);
        sBingoStarSelectorModels[i]->oStarSelectorSize = 1.0;
        sBingoStarSelectorModels[i]->oStarSelectorType = STAR_SELECTOR_100_COINS;
    }
    #undef ROW_WIDTH
    obj_enable_rendering_func(sBingoStarSelectorModels[gBingoStarSelected]);
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
            if ((stars & (1 << i)) || i + 1 == sInitSelectedActNum) {
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

    // Bingo star selection handling
    if (gBingoModifierScrollLockoutTimer > 0) {
        gBingoModifierScrollLockoutTimer--;
    } else {
        if (gPlayer1Controller->buttonDown & R_TRIG) {
            gBingoModifierScrollLockoutTimer = 5;
            play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
            // obj_disable_rendering_func(sBingoStarSelectorModels[gBingoStarSelected]);
            if (gBingoStarSelected == BINGO_MODIFIER_MAX) {
                gBingoStarSelected = BINGO_MODIFIER_NONE;
            } else {
                gBingoStarSelected += 1;
            }
            obj_enable_rendering_func(sBingoStarSelectorModels[gBingoStarSelected]);
            // TODO: Use oOpacity to fade in/out selections.
            // sBingoStarSelectorModels[gBingoStarSelected]->oOpacity /= 2;
        } else if (gPlayer1Controller->buttonDown & Z_TRIG) {
            gBingoModifierScrollLockoutTimer = 5;
            play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
            // obj_disable_rendering_func(sBingoStarSelectorModels[gBingoStarSelected]);
            if (gBingoStarSelected == 0) {
                gBingoStarSelected = BINGO_MODIFIER_MAX;
            } else {
                gBingoStarSelected -= 1;
            }
            obj_enable_rendering_func(sBingoStarSelectorModels[gBingoStarSelected]);
        }
    }
    for (i = BINGO_MODIFIER_NONE; i < BINGO_STARS_TOTAL_AMOUNT; i++) {
        if (i == gBingoStarSelected) {
            sBingoStarSelectorModels[i]->oStarSelectorType = STAR_SELECTOR_SELECTED;
        } else {
            sBingoStarSelectorModels[i]->oStarSelectorType = STAR_SELECTOR_NOT_SELECTED;
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

u8 gBingoTextPressLOrR[] = { BINGO_PRESS_L_OR_R };
u8 gBingoTextGreenDemon[] = { BINGO_GREEN_DEMON };
u8 gBingoTextReverseJoystick[] = { BINGO_REVERSE_JOYSTICK };
u8 gBingoTextClickGame[] = { BINGO_CLICK_GAME };
u8 gBingoTextDaredevil[] = { BINGO_DAREDEVIL_1HP };
u8 gBingoTextRandomRedCoins[] = { BINGO_RANDOM_ROUTE_RED_COINS };
u8 gBingoTextSplatoon[] = { BINGO_SPLATOON };

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

    u8 bingoModifierText[] = { BINGO_MODIFIER };
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

    gSPDisplayList(gDisplayListHead++, dl_menu_ia8_text_begin);
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

    // Print the numbers above each star.
    for (i = 1; i <= sVisibleStars; i++) {
        starNumbers[0] = i;
        print_menu_generic_string(i * 34 - sVisibleStars * 17 + 139, 15, starNumbers);
    }


    switch (gBingoStarSelected) {
        case BINGO_MODIFIER_NONE:
            bingoModifierName = gBingoTextPressLOrR;
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
    }

    print_menu_generic_string(
        get_str_x_pos_from_center(159, bingoModifierText, 10.0f), 115, bingoModifierText);
    print_menu_generic_string(
        get_str_x_pos_from_center(159, bingoModifierName, 10.0f), 128, bingoModifierName);

    gSPDisplayList(gDisplayListHead++, dl_menu_ia8_text_end);
}

/**
 * Geo function that Print act selector strings.
 *!@bug: This geo function is missing the third param. Harmless in practice due to o32 convention.
 */
#ifdef AVOID_UB
Gfx *geo_act_selector_strings(s16 callContext, UNUSED struct GraphNode *node, UNUSED void *context) {
#else
Gfx *geo_act_selector_strings(s16 callContext, UNUSED struct GraphNode *node) {
#endif
    if (callContext == GEO_CONTEXT_RENDER) {
        print_act_selector_strings();
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
#ifndef VERSION_EU
        if ((gPlayer3Controller->buttonPressed & A_BUTTON)
         || (gPlayer3Controller->buttonPressed & START_BUTTON)
         || (gPlayer3Controller->buttonPressed & B_BUTTON)) {
#else
        if ((gPlayer3Controller->buttonPressed & (A_BUTTON | START_BUTTON | B_BUTTON | Z_TRIG))) {
#endif
#ifdef VERSION_JP
            play_sound(SOUND_MENU_STAR_SOUND, gGlobalSoundSource);
#else
            play_sound(SOUND_MENU_STAR_SOUND_LETS_A_GO, gGlobalSoundSource);
#endif
#if ENABLE_RUMBLE
            queue_rumble_data(60, 70);
            func_sh_8024C89C(1);
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
