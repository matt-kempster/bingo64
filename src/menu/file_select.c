#ifndef TARGET_N64
#include <stdlib.h>
#endif
#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include <PR/os_libc.h>

#include "audio/external.h"
#include "behavior_data.h"
#include "dialog_ids.h"
#include "engine/behavior_script.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "file_select.h"
#include "gfx_dimensions.h"
#include "game/area.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/print.h"
#include "game/rumble_init.h"
#include "game/save_file.h"
#include "game/segment2.h"
#include "game/segment7.h"
#include "game/spawn_object.h"
#include "sm64.h"
#include "text_strings.h"
#include "file_select.h"
#include "dialog_ids.h"
#include "game/bingo.h"
#include "game/bingo_board_setup.h"
#include "game/bingo_ui.h"
#include "game/bingo_objective_info.h"
#include "game/strcpy.h"
#include "engine/rand.h"
#include "game/save_file.h"

#ifdef MOUSE_ACTIONS
#include "pc/controller/controller_mouse.h"
#include "pc/configfile.h"
#endif

#ifndef TARGET_N64
#include <stdio.h>
#include "online_lobby.h"
#include "pc/network/network.h"
#include "pc/controller/text_input.h"
#endif

#ifndef TARGET_N64
// The settings door hosts the regular options menu (needs EXT_OPTIONS_MENU,
// which the PC build always enables).
#include "extras/options_menu.h"
#endif

#ifdef COMMAND_LINE_OPTIONS
#include "pc/cliopts.h"
#endif

#include "eu_translation.h"
#ifdef VERSION_EU
#undef LANGUAGE_FUNCTION
#define LANGUAGE_FUNCTION sLanguageMode
#endif

#ifdef VERSION_CN
#define FILE_SELECT_PRINT_STRING print_generic_string
#define FILE_SELECT_TEXT_DL_BEGIN dl_ia_text_begin
#define FILE_SELECT_TEXT_DL_END dl_ia_text_end
#else
#define FILE_SELECT_PRINT_STRING print_menu_generic_string
#define FILE_SELECT_TEXT_DL_BEGIN dl_menu_ia8_text_begin
#define FILE_SELECT_TEXT_DL_END dl_menu_ia8_text_end
#endif

/**
 * @file file_select.c
 * This file implements how the file select and it's menus render and function.
 * That includes button IDs rendered as object models, strings, hand cursor,
 * special menu messages and phases, button states and button clicked checks.
 */

#ifdef VERSION_US
// The current sound mode is automatically centered on US and Shindou.
static s16 sSoundTextX;
#endif

//! @Bug (UB Array Access) For EU, more buttons were added than the array was extended.
//! This causes no currently known issues on console (as the other variables are not changed
//! while this is used) but can cause issues with other compilers.
#if defined(VERSION_EU) && !defined(AVOID_UB)
#define NUM_BUTTONS (MENU_BUTTON_OPTION_MAX - 1)
#else
// Bingo64 replaces the score/copy/erase menus with the seed menu.
#define NUM_BUTTONS 35
#endif

// Amount of main menu buttons defined in the code called by spawn_object_rel_with_rot.
// See file_select.h for the names in MenuButtonTypes.
static struct Object *sMainMenuButtons[NUM_BUTTONS];

// Unused variable that is written to for the centered X value for some strings.
#ifdef VERSION_EU
static s16 sCenteredX;
#endif

// The button that is selected when it is clicked.
static s8 sSelectedButtonID = MENU_BUTTON_NONE;

// On iQue, the courses can't all fit on one screen; there are two pages,
// switched between with the L and R triggers.
#ifdef VERSION_CN
static s8 sScorePage = 0;
#endif

// Whether we are on the main menu or one of the submenus.
static s8 sCurrentMenuLevel = MENU_LAYER_MAIN;

// Used for text opacifying. If it is below 250, it is constantly incremented.
static u8 sTextBaseAlpha = 0;

// 2D position of the cursor on the screen.
// sCursorPos[0]: X | sCursorPos[1]: Y
static f32 sCursorPos[] = {0, 0};

// Determines which graphic to use for the cursor.
static s16 sCursorClickingTimer = 0;

// Equal to sCursorPos if the cursor gets clicked, {-10000, -10000} otherwise.
static s16 sClickPos[] = {-10000, -10000};

// Whether to fade out text or not.
static s8 sFadeOutText = FALSE;

// Used for text fading. The alpha value of text is calculated as
// sTextBaseAlpha - sTextFadeAlpha.
static u8 sTextFadeAlpha = 0;

// File select timer that keeps counting until it reaches 1000.
// Used to prevent buttons from being clickable as soon as a menu loads.
// Gets reset when you click an empty save, existing saves in copy and erase menus
// and when you click yes/no in the erase confirmation prompt.
static s16 sMainMenuTimer = 0;

// Sound mode menu buttonID, has different values compared to gSoundMode in audio.
// 0: gSoundMode = 0 (Stereo) | 1: gSoundMode = 3 (Mono) | 2: gSoundMode = 1 (Headset)
static s8 sSoundMode = 0;

// Defines the value of the save slot selected in the menu.
// Mario A: 1 | Mario B: 2 | Mario C: 3 | Mario D: 4
static s8 sSelectedFileNum = 0;

// Which coin score mode to use when scoring files. 0 for local
// coin high score, 1 for high score across all files.
static s8 sScoreFileCoinScoreMode = 0;

// In EU, if no save file exists, open the language menu so the user can find it.
#ifdef VERSION_EU
static s8 sOpenLangSettings = FALSE;
#endif

static unsigned char textReturn[] = { TEXT_RETURN };
static unsigned char textMarioA[] = { TEXT_FILE_MARIO_A };
static unsigned char textMarioB[] = { TEXT_FILE_MARIO_B };
static unsigned char textMarioC[] = { TEXT_FILE_MARIO_C };
static unsigned char textMarioD[] = { TEXT_FILE_MARIO_D };

#ifndef VERSION_EU
static u8 textNew[] = { TEXT_NEW };
static u8 starIcon[] = { GLYPH_STAR, GLYPH_SPACE };
static u8 xIcon[] = { GLYPH_MULTIPLY, GLYPH_SPACE };
#endif

static unsigned char textSelectFile[] = { TEXT_SELECT_FILE };
static unsigned char textSeeds[] = { TEXT_SEEDS };
#ifdef TARGET_N64
static unsigned char textReset[] = { TEXT_RESET };
#endif
static unsigned char textRandom[] = { TEXT_RANDOM };
#ifdef TARGET_N64
static unsigned char textOption[] = { TEXT_OPTION };
static unsigned char textStart[] = { TEXT_START };
#endif
#ifdef TARGET_N64
static unsigned char textBackspace[] = { TEXT_BACKSPACE };
#endif
static unsigned char textOff[] = { TEXT_OFF };
static unsigned char textOn[] = { TEXT_ON };


s32 gBingoSeedIsSet = 0;
// We can support seeds up to 4,294,967,295, but since this is a weird number,
// we cap it at 999,999,999, which is 9 digits long. "RANDOM" is 6 characters
// long, so:
u8 gBingoSeedRandomText[] = { TEXT_RANDOM 0xFF, 0xFF, 0xFF };
u8 gBingoSeedText[] = { TEXT_RANDOM 0xFF, 0xFF, 0xFF };

// The options screen is ONE vertically scrolling document (see the
// "Options document" section below): settings rows, then the objectives
// (control row + icon bands), then the credits and key hints. The focus
// is in one section at a time: sBingoOptionSelection is the settings row,
// the grid keeps its own cursor (sGridBand/sGridCol), and the credits
// have nothing selectable. L/R jump between the sections' starts.
s32 sBingoOptionSelection = 0;
#ifndef TARGET_N64
// The Opp. rows (visibility of other players' squares/locations) only
// mean something in an online room; solo shows mode/unlock/nonstop/timeout.
// (Preset and Toggle all live on the grid pages' control row.) All uses
// are runtime expressions, so the count may vary per frame.
#define BINGO_CONFIGS_IN_LEFT_COL (network_active() ? 6 : 4)
#else
#define BINGO_CONFIGS_IN_LEFT_COL 4 // not more than 10, hopefully
#endif
s32 sBingoOptionSelectTimer = 0;
#define BINGO_OPTION_TIMER_FRAMES 3
s32 sToggleCurrentOption = 0;
#define OPTIONS_FOCUS_SETTINGS 0
#define OPTIONS_FOCUS_GRID     1
#define OPTIONS_FOCUS_CREDITS  2
static s32 sOptionsFocus = OPTIONS_FOCUS_SETTINGS;
// Document scroll, in units (0 = top). The target is set when the focus
// moves or an arrow is clicked; sOptionsScroll eases toward it once per
// logic frame, and sOptionsScrollPx is its rounded value, which every
// draw and hit test uses (so the draw paths stay integer).
static f32 sOptionsScroll = 0.0f;
static s32 sOptionsScrollTarget = 0;
static s32 sOptionsScrollPx = 0;

// Every entry into the options screen starts at the top of the document.
#ifdef MOUSE_ACTIONS
static f32 sOptionsHoverLastX = -10000.0f, sOptionsHoverLastY = -10000.0f;
static s32 sOptionsHoverArmed = 0;
#endif

static void options_reset(void) {
#ifdef MOUSE_ACTIONS
    sOptionsHoverArmed = 0;
#endif
    sOptionsFocus = OPTIONS_FOCUS_SETTINGS;
    sBingoOptionSelection = 0;
    sOptionsScroll = 0.0f;
    sOptionsScrollTarget = 0;
    sOptionsScrollPx = 0;
}

// Where leaving the options screen lands: the main screen normally, or
// the 1P setup screen / online lobby when it was opened from there
// (vanilla-style submenu-to-submenu navigation).
static s8 sOptionsReturnTarget = MENU_BUTTON_NONE;

#ifndef TARGET_N64
// Room settings (mode/objectives/seed) belong to the host and freeze for
// everyone once the race starts.
static s32 bingo_options_locked(void) {
    return network_active() && (!network_is_host() || network_room_locked());
}
#endif


/**
 * Yellow Background Menu Initial Action
 * Rotates the background at 180 grades and it's scale.
 * Although the scale is properly applied in the loop function.
 */
void beh_yellow_background_menu_init(void) {
    gCurrentObject->oFaceAngleYaw = 0x8000;
    gCurrentObject->oMenuButtonScale = 9.0f;
#ifdef WIDESCREEN
    gCurrentObject->oAnimState = 1;
#endif
}

/**
 * Yellow Background Menu Loop Action
 * Properly scales the background in the main menu.
 */
void beh_yellow_background_menu_loop(void) {
    cur_obj_scale(9.0f);
}

/**
 * Check if a button was clicked.
 * depth = 200.0 for main menu, 22.0 for submenus.
 */
s32 check_clicked_button(s16 x, s16 y, f32 depth) {
    f32 a = 52.4213;
    f32 newX = ((f32) x * 160.0) / (a * depth);
    f32 newY = ((f32) y * 120.0) / (a * 3.0f / 4.0f * depth);
    // s16 maxX = newX + 25.0f;
    // s16 minX = newX - 25.0f;
    // s16 maxY = newY + 21.0f;
    // s16 minY = newY - 21.0f;
    s16 maxX = newX + 20.0f;
    s16 minX = newX - 30.0f;
    s16 maxY = newY + 31.0f;
    s16 minY = newY - 11.0f;
    if (sClickPos[0] < maxX && minX < sClickPos[0] && sClickPos[1] < maxY && minY < sClickPos[1]) {
        return TRUE;
    }
    return FALSE;
}

/**
 * Grow from main menu, used by selecting files and menus.
 */
static void bhv_menu_button_growing_from_main_menu(struct Object *button) {
    if (button->oMenuButtonTimer < 16) {
        button->oFaceAngleYaw += 0x800;
    }
    if (button->oMenuButtonTimer < 8) {
        button->oFaceAnglePitch += 0x800;
    }
    if (button->oMenuButtonTimer >= 8 && button->oMenuButtonTimer < 16) {
        button->oFaceAnglePitch -= 0x800;
    }
    button->oParentRelativePosX -= button->oMenuButtonOrigPosX / 16.0;
    button->oParentRelativePosY -= button->oMenuButtonOrigPosY / 16.0;
    if (button->oPosZ < button->oMenuButtonOrigPosZ + 17800.0) {
        button->oParentRelativePosZ += 1112.5;
    }
    button->oMenuButtonTimer++;
    if (button->oMenuButtonTimer == 16) {
        button->oParentRelativePosX = 0.0f;
        button->oParentRelativePosY = 0.0f;
        button->oMenuButtonState = MENU_BUTTON_STATE_FULLSCREEN;
#ifdef WIDESCREEN
        button->oAnimState = 1;
#endif
        button->oMenuButtonTimer = 0;
    }
}

/**
 * Shrink back to main menu, used to return back while inside menus.
 */
static void bhv_menu_button_shrinking_to_main_menu(struct Object *button) {
    if (button->oMenuButtonTimer < 16) {
        button->oFaceAngleYaw -= 0x800;
    }
    if (button->oMenuButtonTimer < 8) {
        button->oFaceAnglePitch -= 0x800;
    }
    if (button->oMenuButtonTimer >= 8 && button->oMenuButtonTimer < 16) {
        button->oFaceAnglePitch += 0x800;
    }
    button->oParentRelativePosX += button->oMenuButtonOrigPosX / 16.0;
    button->oParentRelativePosY += button->oMenuButtonOrigPosY / 16.0;
    if (button->oPosZ > button->oMenuButtonOrigPosZ) {
        button->oParentRelativePosZ -= 1112.5;
    }
    button->oMenuButtonTimer++;
#ifdef WIDESCREEN
    button->oAnimState = 0;
#endif

    if (button->oMenuButtonTimer == 16) {
        button->oParentRelativePosX = button->oMenuButtonOrigPosX;
        button->oParentRelativePosY = button->oMenuButtonOrigPosY;
        button->oMenuButtonState = MENU_BUTTON_STATE_DEFAULT;
        button->oMenuButtonTimer = 0;
    }
}

/**
 * Grow from submenu, used by selecting a file in the score menu.
 */
static void bhv_menu_button_growing_from_submenu(struct Object *button) {
    if (button->oMenuButtonTimer < 16) {
        button->oFaceAngleYaw += 0x800;
    }
    if (button->oMenuButtonTimer < 8) {
        button->oFaceAnglePitch += 0x800;
    }
    if (button->oMenuButtonTimer >= 8 && button->oMenuButtonTimer < 16) {
        button->oFaceAnglePitch -= 0x800;
    }
    button->oParentRelativePosX -= button->oMenuButtonOrigPosX / 16.0;
    button->oParentRelativePosY -= button->oMenuButtonOrigPosY / 16.0;
    button->oParentRelativePosZ -= 116.25;
    button->oMenuButtonTimer++;
    if (button->oMenuButtonTimer == 16) {
        button->oParentRelativePosX = 0.0f;
        button->oParentRelativePosY = 0.0f;
        button->oMenuButtonState = MENU_BUTTON_STATE_FULLSCREEN;
#ifdef WIDESCREEN
        // Backdrop duty: stretch to the window edges like the main doors.
        button->oAnimState = 1;
#endif
        button->oMenuButtonTimer = 0;
    }
}

/**
 * Shrink back to submenu, used to return back while inside a score save menu.
 */
static void bhv_menu_button_shrinking_to_submenu(struct Object *button) {
#ifdef WIDESCREEN
    button->oAnimState = 0;
#endif
    if (button->oMenuButtonTimer < 16) {
        button->oFaceAngleYaw -= 0x800;
    }
    if (button->oMenuButtonTimer < 8) {
        button->oFaceAnglePitch -= 0x800;
    }
    if (button->oMenuButtonTimer >= 8 && button->oMenuButtonTimer < 16) {
        button->oFaceAnglePitch += 0x800;
    }
    button->oParentRelativePosX += button->oMenuButtonOrigPosX / 16.0;
    button->oParentRelativePosY += button->oMenuButtonOrigPosY / 16.0;
    if (button->oPosZ > button->oMenuButtonOrigPosZ) {
        button->oParentRelativePosZ += 116.25;
    }
    button->oMenuButtonTimer++;
    if (button->oMenuButtonTimer == 16) {
        button->oParentRelativePosX = button->oMenuButtonOrigPosX;
        button->oParentRelativePosY = button->oMenuButtonOrigPosY;
        button->oMenuButtonState = MENU_BUTTON_STATE_DEFAULT;
        button->oMenuButtonTimer = 0;
    }
}

/**
 * A small increase and decrease in size.
 * Used by failed copy/erase/score operations and sound mode select.
 */
static void bhv_menu_button_zoom_in_out(struct Object *button) {
    if (sCurrentMenuLevel == MENU_LAYER_MAIN) {
        if (button->oMenuButtonTimer < 4) {
            button->oParentRelativePosZ -= 40.0f;
        }
        if (button->oMenuButtonTimer >= 4) {
            button->oParentRelativePosZ += 40.0f;
        }
    } else {
        if (button->oMenuButtonTimer < 4) {
            button->oParentRelativePosZ += 20.0f;
        }
        if (button->oMenuButtonTimer >= 4) {
            button->oParentRelativePosZ -= 20.0f;
        }
    }
    button->oMenuButtonTimer++;
    if (button->oMenuButtonTimer == 8) {
        button->oMenuButtonState = MENU_BUTTON_STATE_DEFAULT;
#ifdef WIDESCREEN
        button->oAnimState = 0;
#endif
        button->oMenuButtonTimer = 0;
    }
}

/**
 * A small temporary increase in size.
 * Used while selecting a target copy/erase file or yes/no erase confirmation prompt.
 */
static void bhv_menu_button_zoom_in(struct Object *button) {
    button->oMenuButtonScale += 0.0022;
    button->oMenuButtonTimer++;
    if (button->oMenuButtonTimer == 10) {
        button->oMenuButtonState = MENU_BUTTON_STATE_DEFAULT;
#ifdef WIDESCREEN
        button->oAnimState = 0;
#endif
        button->oMenuButtonTimer = 0;
    }
}

/**
 * A small temporary decrease in size.
 * Used after selecting a target copy/erase file or
 * yes/no erase confirmation prompt to undo the zoom in.
 */
static void bhv_menu_button_zoom_out(struct Object *button) {
    button->oMenuButtonScale -= 0.0022;
    button->oMenuButtonTimer++;
    if (button->oMenuButtonTimer == 10) {
        button->oMenuButtonState = MENU_BUTTON_STATE_DEFAULT;
#ifdef WIDESCREEN
        button->oAnimState = 0;
#endif
        button->oMenuButtonTimer = 0;
    }
}

/**
 * Menu Buttons Menu Initial Action
 * Aligns menu buttons so they can stay in their original
 * positions when you choose a button.
 */
void bhv_menu_button_init(void) {
    gCurrentObject->oMenuButtonOrigPosX = gCurrentObject->oParentRelativePosX;
    gCurrentObject->oMenuButtonOrigPosY = gCurrentObject->oParentRelativePosY;
#ifdef WIDESCREEN
    gCurrentObject->oAnimState = 0;
#endif
}

/**
 * Menu Buttons Menu Loop Action
 * Handles the functions of the button states and
 * object scale for each button.
 */
void bhv_menu_button_loop(void) {
    switch (gCurrentObject->oMenuButtonState) {
        case MENU_BUTTON_STATE_DEFAULT: // Button state
            gCurrentObject->oMenuButtonOrigPosZ = gCurrentObject->oPosZ;
            break;
        case MENU_BUTTON_STATE_GROWING: // Switching from button to menu state
            if (sCurrentMenuLevel == MENU_LAYER_MAIN) {
                bhv_menu_button_growing_from_main_menu(gCurrentObject);
            }
            if (sCurrentMenuLevel == MENU_LAYER_SUBMENU) {
                bhv_menu_button_growing_from_submenu(gCurrentObject); // Only used for score files
            }
            sTextBaseAlpha = 0;
            sCursorClickingTimer = 4;
            break;
        case MENU_BUTTON_STATE_FULLSCREEN: // Menu state
            break;
        case MENU_BUTTON_STATE_SHRINKING: // Switching from menu to button state
            if (sCurrentMenuLevel == MENU_LAYER_MAIN) {
                bhv_menu_button_shrinking_to_main_menu(gCurrentObject);
            }
            if (sCurrentMenuLevel == MENU_LAYER_SUBMENU) {
                bhv_menu_button_shrinking_to_submenu(gCurrentObject); // Only used for score files
            }
            sTextBaseAlpha = 0;
            sCursorClickingTimer = 4;
            break;
        case MENU_BUTTON_STATE_ZOOM_IN_OUT:
            bhv_menu_button_zoom_in_out(gCurrentObject);
            sCursorClickingTimer = 0;
            break;
        case MENU_BUTTON_STATE_ZOOM_IN:
            bhv_menu_button_zoom_in(gCurrentObject);
            sCursorClickingTimer = 4;
            break;
        case MENU_BUTTON_STATE_ZOOM_OUT:
            bhv_menu_button_zoom_out(gCurrentObject);
            sCursorClickingTimer = 4;
            break;
    }
    cur_obj_scale(gCurrentObject->oMenuButtonScale);
}

/**
 * Handles how to exit the score file menu using button states.
 */
void exit_score_file_to_score_menu(struct Object *scoreFileButton, s8 scoreButtonID) {
    // Begin exit
    if (scoreFileButton->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN
        && sCursorClickingTimer == 2) {
        play_sound(SOUND_MENU_CAMERA_ZOOM_OUT, gGlobalSoundSource);
#ifdef RUMBLE_FEEDBACK
        queue_rumble_data(5, 80);
#endif
        scoreFileButton->oMenuButtonState = MENU_BUTTON_STATE_SHRINKING;
    }
    // End exit
    if (scoreFileButton->oMenuButtonState == MENU_BUTTON_STATE_DEFAULT) {
        sSelectedButtonID = scoreButtonID;
        if (sCurrentMenuLevel == MENU_LAYER_SUBMENU) {
            sCurrentMenuLevel = MENU_LAYER_MAIN;
        }
    }
}

#ifndef TARGET_N64
// --- PC submenu screens: 1P setup and the online lobby --------------------
// Each is a fullscreen-grown main button carrying small 3D action buttons
// (vanilla score-menu style). Exits are explicit flags: a click on a
// sub-button must not double as "leave the screen".

static s8 s1PExitRequest = 0;
static s8 sLobbyExitRequest = 0;
static s8 sSettingsExitRequest = 0;
static s8 sSettingsMenuEntered = 0;

static void spawn_submenu_button(s32 id, s32 model, struct Object *parent,
                                 s16 x, s16 y, f32 scale) {
    sMainMenuButtons[id] = spawn_object_rel_with_rot(
        parent, model, bhvMenuButton, x, y, -100, 0, -0x8000, 0);
    // spawn_object_rel_with_rot keeps the vanilla zOff-as-roll typo; these
    // buttons want an exact zero roll, so set the angle explicitly.
    obj_set_angle(sMainMenuButtons[id], 0, -0x8000, 0);
    sMainMenuButtons[id]->oMenuButtonScale = scale;
}

// A symmetric, generous hitbox for the PC submenu 3D buttons, covering the
// rendered button plus the label under it. check_clicked_button's box is
// skewed 30-left/20-right for the N64 hand sprite, which left the right
// half of a button (and its whole label) unclickable.
static s32 check_clicked_submenu_button(struct Object *btn) {
    f32 x = btn->oPosX / 7.208f;
    f32 y = btn->oPosY / 7.208f;
    return sClickPos[0] > x - 30.0f && sClickPos[0] < x + 30.0f
        && sClickPos[1] > y - 34.0f && sClickPos[1] < y + 26.0f;
}

static void delete_submenu_button(s32 id) {
    if (sMainMenuButtons[id] != NULL) {
        obj_mark_for_deletion(sMainMenuButtons[id]);
        sMainMenuButtons[id] = NULL;
    }
}

// Shrink out of a fullscreen submenu screen once its exit flag is raised,
// then hand control back to targetID.
static void exit_submenu_screen(struct Object *button, s8 targetID, s8 *exitFlag) {
    if (!*exitFlag) {
        return;
    }
    if (button->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN) {
        play_sound(SOUND_MENU_CAMERA_ZOOM_OUT, gGlobalSoundSource);
        button->oMenuButtonState = MENU_BUTTON_STATE_SHRINKING;
    } else if (button->oMenuButtonState == MENU_BUTTON_STATE_DEFAULT) {
        sSelectedButtonID = targetID;
        *exitFlag = 0;
    }
}

// Open the OPTIONS screen: the clicked OPTIONS sub-button itself grows
// fullscreen from its spot (the vanilla score-file zoom) and doubles as
// the screen's backdrop; the SEED_OPTION slot aliases it until the screen
// closes and it shrinks back into place. B or BACK returns to returnTarget.
static void open_options_screen(s8 returnTarget, struct Object *sourceBtn) {
    play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
    gOptionSelectIconOpacity = 0;
    sTextBaseAlpha = 0;
    sMainMenuButtons[MENU_BUTTON_SEED_OPTION] = sourceBtn;
    // Backdrop coverage matches the vanilla score-file zoom's 1/9 scale;
    // restored to the screen's button size when the exit completes.
    sourceBtn->oMenuButtonScale = 0.11111111f;
    sourceBtn->oMenuButtonState = MENU_BUTTON_STATE_GROWING;
    sCurrentMenuLevel = MENU_LAYER_SUBMENU;
    sSelectedButtonID = MENU_BUTTON_SEED_OPTION;
    sOptionsReturnTarget = returnTarget;
    options_reset();
}
#endif

static void seed_push_key(s32 key) {
    s32 i;
    if (!gBingoSeedIsSet) {
        // First keypress
        for (i = 0; i < 9; i++) {
            gBingoSeedText[i] = 0x00;
        }
        gBingoSeedIsSet = 1;
    }
    // Shift everything by 1 and add key to back
    for (i = 0; i < (9 - 1); i++) {
        gBingoSeedText[i] = gBingoSeedText[i + 1];
    }
    gBingoSeedText[8] = key;
}

static void seed_reset(void) {
    s32 i;
    gBingoSeedIsSet = 0;
    for (i = 0; i < 10; i++) {
        gBingoSeedText[i] = gBingoSeedRandomText[i];
    }
}

#undef BUZZ_TIMER

static void seed_backspace(void) {
    s32 i;
    if (gBingoSeedIsSet) {
        for (i = 8; i > 0; i--) {
            gBingoSeedText[i] = gBingoSeedText[i - 1];
        }
        gBingoSeedText[0] = 0x00;
    }
}

// The seed value shared by every entry UI (numpad on N64; keyboard on
// the PC main screen and the online lobby) and pushed to the room as the
// host's proposal. 0 = random.
u32 bingo_seed_proposal(void) {
    if (!gBingoSeedIsSet) {
        return 0;
    }
    return (
        gBingoSeedText[0] * 100000000
        + gBingoSeedText[1] * 10000000
        + gBingoSeedText[2] * 1000000
        + gBingoSeedText[3] * 100000
        + gBingoSeedText[4] * 10000
        + gBingoSeedText[5] * 1000
        + gBingoSeedText[6] * 100
        + gBingoSeedText[7] * 10
        + gBingoSeedText[8] * 1
    );
}

void bingo_seed_set_from_ascii(const char *str) {
    s32 i;
    seed_reset();
    for (i = 0; str[i] != '\0'; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            seed_push_key(str[i] - '0');
        }
    }
}

// "" when random; digits without leading zeros otherwise.
void bingo_seed_to_ascii(char *out, s32 size) {
    s32 i, o = 0, seen = 0;
    if (gBingoSeedIsSet) {
        for (i = 0; i < 9 && o < size - 1; i++) {
            if (gBingoSeedText[i] != 0) {
                seen = 1;
            }
            if (seen || i == 8) {
                out[o++] = '0' + gBingoSeedText[i];
            }
        }
    }
    out[o] = '\0';
}

#undef ACTION_TIMER
#undef MAIN_RETURN_TIMER

#ifdef VERSION_EU
    #define SOUND_BUTTON_Y 388
#else
    #define SOUND_BUTTON_Y 0
#endif

#ifndef TARGET_N64
// The keyboard seed entry on the PC main screen (replaces the numpad).
char gSeedTypedBuf[16];
s32 gSeedTypingActive = 0;

static void seed_typing_begin(void) {
    bingo_seed_to_ascii(gSeedTypedBuf, sizeof(gSeedTypedBuf));
    text_input_start(gSeedTypedBuf, 10);  // 9 digits + terminator
    gSeedTypingActive = 1;
}

static void seed_typing_finish(void) {
    text_input_stop();
    gSeedTypingActive = 0;
    bingo_seed_set_from_ascii(gSeedTypedBuf);
}

void load_main_menu_save_file(struct Object *fileButton, s32 fileNum);

// The 1P setup screen: the green door grown fullscreen, with the seed
// display (click to type) and small START / OPTIONS buttons.
static void onep_screen_loop(void) {
    struct Object *door = sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A];

    if (door->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN
        && sMainMenuButtons[MENU_BUTTON_1P_START] == NULL && !s1PExitRequest
        && sSelectedFileNum == 0) {
        // The fullscreen parent is yaw-rotated 180, so child X mirrors:
        // spawn at -330 to render on the right.
        spawn_submenu_button(MENU_BUTTON_1P_START,
                             MODEL_MAIN_MENU_YELLOW_FILE_BUTTON, door, -330, -320,
                             0.11111111f);
        spawn_submenu_button(MENU_BUTTON_1P_OPTIONS,
                             MODEL_MAIN_MENU_BLUE_COPY_BUTTON, door, 330, -320,
                             0.11111111f);
    }

    if (sMainMenuButtons[MENU_BUTTON_1P_START] != NULL
        && door->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN
        && !gSeedTypingActive) {
        if (check_clicked_submenu_button(sMainMenuButtons[MENU_BUTTON_1P_START])) {
            play_sound(SOUND_MENU_STAR_SOUND_OKEY_DOKEY, gGlobalSoundSource);
            // Buttons vanish for the fade-out; updates freeze once the
            // level script sees the file number, so leave a clean frame.
            delete_submenu_button(MENU_BUTTON_1P_START);
            delete_submenu_button(MENU_BUTTON_1P_OPTIONS);
            load_main_menu_save_file(door, 1);
        } else if (check_clicked_submenu_button(
                       sMainMenuButtons[MENU_BUTTON_1P_OPTIONS])) {
            // START stays alive behind the grown options screen and is
            // revealed again when it shrinks back.
            open_options_screen(MENU_BUTTON_PLAY_FILE_A,
                                sMainMenuButtons[MENU_BUTTON_1P_OPTIONS]);
        } else if (sClickPos[0] > -65 && sClickPos[0] < 65
                   && sClickPos[1] > 5 && sClickPos[1] < 60) {
            seed_typing_begin();
        }
    }

    if (s1PExitRequest) {
        delete_submenu_button(MENU_BUTTON_1P_START);
        delete_submenu_button(MENU_BUTTON_1P_OPTIONS);
    }
    exit_submenu_screen(door, MENU_BUTTON_NONE, &s1PExitRequest);
}

// The online lobby: the yellow door grown fullscreen; field rows are
// handled by online_lobby.c, the four action buttons live here.
static void lobby_screen_loop(void) {
    struct Object *door = sMainMenuButtons[MENU_BUTTON_ONLINE];
    s32 b;

    if (door->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN
        && sMainMenuButtons[MENU_BUTTON_LOBBY_CONNECT] == NULL
        && !sLobbyExitRequest && sSelectedFileNum == 0) {
        static const s32 models[LOBBY_BTN_COUNT] = {
            MODEL_MAIN_MENU_YELLOW_FILE_BUTTON,  // CONNECT / LEAVE (red would
                                                 // vanish into the red room)
            MODEL_MAIN_MENU_PURPLE_SOUND_BUTTON, // READY
            MODEL_MAIN_MENU_BLUE_COPY_BUTTON,    // OPTIONS
            MODEL_MAIN_MENU_GREEN_SCORE_BUTTON,  // START RACE
        };
        for (b = 0; b < LOBBY_BTN_COUNT; b++) {
            s16 x, y;
            online_lobby_button_world_pos(b, &x, &y);
            // Uniform size for all four; varying it to mark inactive buttons
            // just read as broken layout. Inactive = dim label + buzz.
            spawn_submenu_button(MENU_BUTTON_LOBBY_MIN + b, models[b], door, x, y,
                                 0.10f);
        }
        // An ENTER pressed on some earlier screen must not pop a field
        // editor open the moment the lobby appears.
        text_input_take_enter_key();
    }

    if (sMainMenuButtons[MENU_BUTTON_LOBBY_CONNECT] != NULL
        && door->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN) {
        for (b = 0; b < LOBBY_BTN_COUNT; b++) {
            struct Object *btn = sMainMenuButtons[MENU_BUTTON_LOBBY_MIN + b];
            if (check_clicked_submenu_button(btn)) {
                if (!online_lobby_button_active(b)) {
                    play_sound(SOUND_MENU_CAMERA_BUZZ, gGlobalSoundSource);
                } else {
                    play_sound(SOUND_MENU_CLICK_FILE_SELECT, gGlobalSoundSource);
                    if (online_lobby_button_pressed(b) == 2) {
                        // The other buttons stay alive behind the grown
                        // options screen until it shrinks back.
                        open_options_screen(MENU_BUTTON_ONLINE, btn);
                    }
                }
                break;
            }
        }
    }

    if (sLobbyExitRequest) {
        for (b = 0; b < LOBBY_BTN_COUNT; b++) {
            delete_submenu_button(MENU_BUTTON_LOBBY_MIN + b);
        }
    }
    exit_submenu_screen(door, MENU_BUTTON_NONE, &sLobbyExitRequest);
}

// The settings screen: the purple door grown fullscreen, hosting the
// regular options menu (controls/display/sound) so everything can be set
// up before starting a game. The pause-menu options code is reused
// wholesale; closing the menu with R saves the config, rebinds the
// controls, and shrinks back to the doors.
static void settings_screen_loop(void) {
    struct Object *door = sMainMenuButtons[MENU_BUTTON_SETTINGS];

    if (door->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN
        && !sSettingsExitRequest) {
        if (!sSettingsMenuEntered) {
            if (!optmenu_open) {
                optmenu_toggle();
            }
            sSettingsMenuEntered = 1;
        } else if (!optmenu_open) {
            // The menu closed itself (R): optmenu_toggle already saved.
            sSettingsMenuEntered = 0;
            sTextBaseAlpha = 0;
            sSettingsExitRequest = 1;
        } else {
            optmenu_check_buttons();
        }
    }
    exit_submenu_screen(door, MENU_BUTTON_NONE, &sSettingsExitRequest);
}
#endif

#ifdef TARGET_N64
static void seed_menu_check_clicked_buttons() {
    int buttonId;

    for (buttonId = MENU_BUTTON_SEED_MIN; buttonId < MENU_BUTTON_SEED_MAX; buttonId++) {
        s16 buttonX = sMainMenuButtons[buttonId]->oPosX;
        s16 buttonY = sMainMenuButtons[buttonId]->oPosY;

        if (check_clicked_button(buttonX, buttonY, 200.0f) == TRUE) {
            switch (buttonId) {
                case MENU_BUTTON_SEED_RESET:
                    sMainMenuButtons[buttonId]->oMenuButtonState = MENU_BUTTON_STATE_ZOOM_IN_OUT;
                    seed_reset();
                    break;
                case MENU_BUTTON_SEED_BACKSPACE:
                    sMainMenuButtons[buttonId]->oMenuButtonState = MENU_BUTTON_STATE_ZOOM_IN_OUT;
                    seed_backspace();
                    break;
                case MENU_BUTTON_SEED_OPTION:
                    play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
                    gOptionSelectIconOpacity = 0;
                    sMainMenuButtons[buttonId]->oMenuButtonState = MENU_BUTTON_STATE_GROWING;
                    sSelectedButtonID = buttonId;
                    options_reset();
                    break;
                case MENU_BUTTON_SEED_NUM_1:
                case MENU_BUTTON_SEED_NUM_2:
                case MENU_BUTTON_SEED_NUM_3:
                case MENU_BUTTON_SEED_NUM_4:
                case MENU_BUTTON_SEED_NUM_5:
                case MENU_BUTTON_SEED_NUM_6:
                case MENU_BUTTON_SEED_NUM_7:
                case MENU_BUTTON_SEED_NUM_8:
                case MENU_BUTTON_SEED_NUM_9:
                    sMainMenuButtons[buttonId]->oMenuButtonState = MENU_BUTTON_STATE_ZOOM_IN_OUT;
                    seed_push_key(buttonId - MENU_BUTTON_SEED_NUM_1 + 1);  // Sort of hacky.
                    break;
                case MENU_BUTTON_SEED_NUM_0:
                    sMainMenuButtons[buttonId]->oMenuButtonState = MENU_BUTTON_STATE_ZOOM_IN_OUT;
                    seed_push_key(0);
                    break;
            }
            break;
        }
    }
}
#endif

/**
 * Loads a save file selected after it goes into a full screen state
 * retuning sSelectedFileNum to a save value defined in fileNum.
 */
void load_main_menu_save_file(struct Object *fileButton, s32 fileNum) {
    if (fileButton->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN) {
        sSelectedFileNum = fileNum;
    }

#ifdef EXT_DEBUG_MENU
    if (gPlayer1Controller->buttonDown == (L_CBUTTONS | D_CBUTTONS)) {
        get_complete_save_file(fileNum);
    }
#endif
}

/**
 * Returns from the previous menu back to the main menu using
 * the return button (or sound mode) as source button.
 */
static void return_to_main_menu(s16 prevMenuButtonID, struct Object *sourceButton) {
    // If the source button is in default state and the previous menu in full screen,
    // play zoom out sound and shrink previous menu
    if (sourceButton->oMenuButtonState == MENU_BUTTON_STATE_DEFAULT
        && sMainMenuButtons[prevMenuButtonID]->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN) {
        play_sound(SOUND_MENU_CAMERA_ZOOM_OUT, gGlobalSoundSource);
        sMainMenuButtons[prevMenuButtonID]->oMenuButtonState = MENU_BUTTON_STATE_SHRINKING;
        sCurrentMenuLevel = MENU_LAYER_MAIN;
    }
}

/**
 * Menu Buttons Menu Manager Initial Action
 * Creates models of the buttons in the menu. For the Mario buttons it
 * checks if a save file exists to render an specific button model for it.
 * Unlike buttons on submenus, these are never hidden or recreated.
 */
void bhv_menu_button_manager_init(void) {
#ifdef TARGET_N64
    enum MenuButtonTypes buttonID;
    u8 buttonNum;
    s16 buttonX;
    s16 buttonY;

    // The numpad only exists on N64, where it is the sole way to type a
    // seed. The PC build enters seeds with the keyboard.
    sMainMenuButtons[MENU_BUTTON_SEED_RESET] = spawn_object_rel_with_rot(
        gCurrentObject, MODEL_MAIN_MENU_RED_ERASE_BUTTON, bhvMenuButton, -6800, -3800, 0, 0, 0, 0
    );
    sMainMenuButtons[MENU_BUTTON_SEED_RESET]->oMenuButtonScale = 1.0f;

    sMainMenuButtons[MENU_BUTTON_SEED_BACKSPACE] = spawn_object_rel_with_rot(
        gCurrentObject, MODEL_MAIN_MENU_PURPLE_SOUND_BUTTON, bhvMenuButton, -6800, 1000, 0, 0, 0, 0
    );
    sMainMenuButtons[MENU_BUTTON_SEED_BACKSPACE]->oMenuButtonScale = 1.0f;

    for (
        buttonID = MENU_BUTTON_SEED_NUMPAD_MIN, buttonNum = 1;
        buttonID <= MENU_BUTTON_SEED_NUMPAD_MAX;
        buttonID++, buttonNum++
    ) {
        switch (buttonNum % 3) {
            case 1:  // Leftmost
                buttonX = -2400;
                break;
            case 2:  // Middle
                buttonX = 0;
                break;
            case 0:  // Rightmost
                buttonX = 2400;
                break;
        }
        switch ((buttonNum - 1) / 3) {
            case 0:  // Top
                buttonY = -1300 + 2200;
                break;
            case 1:  // Middle
                buttonY = -1300;
                break;
            case 2:  // Bottom
                buttonY = -1300 - 2200;
                break;
        }
        sMainMenuButtons[buttonID] = spawn_object_rel_with_rot(
            gCurrentObject, MODEL_MAIN_MENU_NUMPAD_0 + buttonNum, bhvMenuButton, buttonX, buttonY, 0, 0, 0, 0
        );
        sMainMenuButtons[buttonID]->oMenuButtonScale = 0.75f;
    }
    sMainMenuButtons[MENU_BUTTON_SEED_NUM_0] = spawn_object_rel_with_rot(
        gCurrentObject, MODEL_MAIN_MENU_NUMPAD_0, bhvMenuButton, 0, -1300 - 4200, 0, 0, 0, 0
    );
    sMainMenuButtons[buttonID]->oMenuButtonScale = 0.75f;
#endif

#ifdef TARGET_N64
    sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A] = spawn_object_rel_with_rot(
        gCurrentObject, MODEL_MAIN_MENU_GREEN_SCORE_BUTTON, bhvMenuButton, 6800, 1000, 0, 0, 0, 0
    );
    sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A]->oMenuButtonScale = 1.0f;

    sMainMenuButtons[MENU_BUTTON_SEED_OPTION] = spawn_object_rel_with_rot(
        gCurrentObject, MODEL_MAIN_MENU_BLUE_COPY_BUTTON, bhvMenuButton, 6800, -3800, 0, 0, 0, 0
    );
    sMainMenuButtons[MENU_BUTTON_SEED_OPTION]->oMenuButtonScale = 1.0f;
#else
    // The PC main screen is three doors: 1P (green, the play/seed setup
    // screen), NET (red, the online lobby) and SETTINGS (purple, the
    // options menu), centered in a row.
    sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A] = spawn_object_rel_with_rot(
        gCurrentObject, MODEL_MAIN_MENU_GREEN_SCORE_BUTTON, bhvMenuButton, -4600, 800, 0, 0, 0, 0
    );
    sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A]->oMenuButtonScale = 1.0f;

    sMainMenuButtons[MENU_BUTTON_ONLINE] = spawn_object_rel_with_rot(
        gCurrentObject, MODEL_MAIN_MENU_RED_ERASE_BUTTON, bhvMenuButton, 0, 800, 0, 0, 0, 0
    );
    sMainMenuButtons[MENU_BUTTON_ONLINE]->oMenuButtonScale = 1.0f;

    sMainMenuButtons[MENU_BUTTON_SETTINGS] = spawn_object_rel_with_rot(
        gCurrentObject, MODEL_MAIN_MENU_PURPLE_SOUND_BUTTON, bhvMenuButton, 4600, 800, 0, 0, 0, 0
    );
    sMainMenuButtons[MENU_BUTTON_SETTINGS]->oMenuButtonScale = 1.0f;

    // The options screen has no backdrop object of its own on PC: the
    // clicked OPTIONS sub-button itself grows fullscreen (vanilla
    // score-file style), and this slot aliases it while the screen is up.
    sMainMenuButtons[MENU_BUTTON_SEED_OPTION] = NULL;

    sMainMenuButtons[MENU_BUTTON_1P_START] = NULL;
    sMainMenuButtons[MENU_BUTTON_1P_OPTIONS] = NULL;
    sMainMenuButtons[MENU_BUTTON_LOBBY_CONNECT] = NULL;
    sMainMenuButtons[MENU_BUTTON_LOBBY_READY] = NULL;
    sMainMenuButtons[MENU_BUTTON_LOBBY_OPTIONS] = NULL;
    sMainMenuButtons[MENU_BUTTON_LOBBY_START] = NULL;
#endif

    sTextBaseAlpha = 0;
}

#if defined(VERSION_JP) || defined(VERSION_SH)
    #define SAVE_FILE_SOUND SOUND_MENU_STAR_SOUND
#else
    #define SAVE_FILE_SOUND SOUND_MENU_STAR_SOUND_OKEY_DOKEY
#endif

/**
 * In the main menu, check if a button was clicked to play it's button growing state.
 * Also play a sound and/or render buttons depending of the button ID selected.
 */
static void check_main_menu_clicked_buttons(void) {
#ifdef TARGET_N64
    // Main Menu buttons
    s8 buttonID;
    // Configure Main Menu button group
    for (buttonID = MENU_BUTTON_MAIN_MIN; buttonID < MENU_BUTTON_MAIN_MAX; buttonID++) {
        s16 buttonX = sMainMenuButtons[buttonID]->oPosX;
        s16 buttonY = sMainMenuButtons[buttonID]->oPosY;

        if (check_clicked_button(buttonX, buttonY, 200.0f) == TRUE) {
            // If menu button clicked, select it
            sMainMenuButtons[buttonID]->oMenuButtonState = MENU_BUTTON_STATE_GROWING;
            sSelectedButtonID = buttonID;
            break;
        }
    }

    // Play sound of the save file clicked
    switch (sSelectedButtonID) {
        case MENU_BUTTON_PLAY_FILE_A:
            play_sound(SAVE_FILE_SOUND, gGlobalSoundSource);
            break;
    }
#else
    // The PC main screen: the 1P door (setup screen) and the NET door
    // (online lobby). Options live inside those screens, not up here.
    if (check_clicked_button(sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A]->oPosX,
                             sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A]->oPosY,
                             200.0f) == TRUE) {
        if (network_active()) {
            // No solo game while in a room; the room's GO starts you.
            play_sound(SOUND_MENU_CAMERA_BUZZ, gGlobalSoundSource);
        } else {
            play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
            sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A]->oMenuButtonState =
                MENU_BUTTON_STATE_GROWING;
            sSelectedButtonID = MENU_BUTTON_PLAY_FILE_A;
        }
    } else if (check_clicked_button(sMainMenuButtons[MENU_BUTTON_ONLINE]->oPosX,
                                    sMainMenuButtons[MENU_BUTTON_ONLINE]->oPosY,
                                    200.0f) == TRUE) {
        play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
        sMainMenuButtons[MENU_BUTTON_ONLINE]->oMenuButtonState =
            MENU_BUTTON_STATE_GROWING;
        sSelectedButtonID = MENU_BUTTON_ONLINE;
    } else if (check_clicked_button(sMainMenuButtons[MENU_BUTTON_SETTINGS]->oPosX,
                                    sMainMenuButtons[MENU_BUTTON_SETTINGS]->oPosY,
                                    200.0f) == TRUE) {
        play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
        sMainMenuButtons[MENU_BUTTON_SETTINGS]->oMenuButtonState =
            MENU_BUTTON_STATE_GROWING;
        sSelectedButtonID = MENU_BUTTON_SETTINGS;
    }
#endif
}

#undef SAVE_FILE_SOUND

/**
 * Menu Buttons Menu Manager Loop Action
 * Calls a menu function depending of the button chosen.
 * sSelectedButtonID is MENU_BUTTON_NONE when the file select
 * is loaded, and that checks what buttonID is clicked in the main menu.
 */
void bhv_menu_button_manager_loop(void) {
#ifndef TARGET_N64
    // A room reset (back to lobby) while we're already at the file
    // select needs no warp; consume the flag so it can't fire later.
    network_take_lobby_return_flag();
    // The room's GO: every player's game launches itself, no clicking.
    if (network_take_go_flag() && sSelectedFileNum == 0) {
        s32 d;
        if (text_input_active()) {
            text_input_stop();
        }
        gSeedTypingActive = 0;
        play_sound(SOUND_MENU_STAR_SOUND_OKEY_DOKEY, gGlobalSoundSource);
        sSelectedFileNum = 1;
        // Clear the screen's action buttons: object updates freeze during
        // the fade-out, and a frozen mid-state button row reads as a glitch.
        delete_submenu_button(MENU_BUTTON_1P_START);
        delete_submenu_button(MENU_BUTTON_1P_OPTIONS);
        for (d = 0; d < LOBBY_BTN_COUNT; d++) {
            delete_submenu_button(MENU_BUTTON_LOBBY_MIN + d);
        }
        // If the options screen was up, its backdrop was one of those
        // buttons; drop the alias so nothing touches the deleted object.
        if (sSelectedButtonID == MENU_BUTTON_SEED_OPTION) {
            sMainMenuButtons[MENU_BUTTON_SEED_OPTION] = NULL;
        }
        // A race GO while the settings menu is up: close it (which saves
        // the config) so it doesn't hang over the fade-out.
        if (optmenu_open) {
            optmenu_toggle();
            sSettingsMenuEntered = 0;
        }
    }
#endif

    switch (sSelectedButtonID) {
        case MENU_BUTTON_NONE:
            check_main_menu_clicked_buttons();
#ifdef TARGET_N64
            seed_menu_check_clicked_buttons();
#endif
            break;
        case MENU_BUTTON_PLAY_FILE_A:
#ifdef TARGET_N64
            load_main_menu_save_file(sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A], 1);
#else
            onep_screen_loop();
#endif
            break;
        case MENU_BUTTON_SEED_OPTION:
#ifndef TARGET_N64
            if (sMainMenuButtons[MENU_BUTTON_SEED_OPTION] == NULL) {
                break;  // its lobby button was deleted at GO mid-options
            }
#endif
            exit_score_file_to_score_menu(sMainMenuButtons[MENU_BUTTON_SEED_OPTION],
                                          sOptionsReturnTarget);
            if (sSelectedButtonID != MENU_BUTTON_SEED_OPTION) {
#ifndef TARGET_N64
                // The backdrop was the clicked OPTIONS sub-button; it has
                // shrunk back into its spot, so it resumes life as that
                // button at its screen's size.
                sMainMenuButtons[MENU_BUTTON_SEED_OPTION]->oMenuButtonScale =
                    (sOptionsReturnTarget == MENU_BUTTON_ONLINE) ? 0.10f
                                                                 : 0.11111111f;
                sMainMenuButtons[MENU_BUTTON_SEED_OPTION] = NULL;
                // ENTERs pressed on the options screen stay there.
                text_input_take_enter_key();
#endif
                sOptionsReturnTarget = MENU_BUTTON_NONE;
            }
            break;
#ifndef TARGET_N64
        case MENU_BUTTON_ONLINE:
            lobby_screen_loop();
            break;
        case MENU_BUTTON_SETTINGS:
            settings_screen_loop();
            break;
#endif
    }

    sClickPos[0] = -10000;
    sClickPos[1] = -10000;
}

// Leave the options screen: fakes the click-timer pulse that
// exit_score_file_to_score_menu waits for to start the shrink-out.
static void options_screen_exit(void) {
    sTextBaseAlpha = 0;
    gOptionSelectIconOpacity = 0;
    sClickPos[0] = sCursorPos[0];
    sClickPos[1] = sCursorPos[1];
    sCursorClickingTimer = 1;
#ifndef TARGET_N64
    // If we host an online room, the options may have changed.
    if (!bingo_options_locked()) {
        network_push_local_options();
    }
#endif
}

// ---------------------------------------------------------------------------
// Options document. Items are placed at document y (units DOWN from the
// document's top, at text baselines / icon bottoms) and drawn at
// options_screen_y(), the menu's bottom-up print coordinate, inside the
// content window [OPT_CONTENT_BOTTOM, OPT_CONTENT_TOP] (scissored). The
// frame's inner top shadow sits above the window, the footer strip below.
#define OPT_CONTENT_TOP    214
#define OPT_CONTENT_BOTTOM 46
#define OPT_DOC_TOP_Y      (OPT_CONTENT_TOP - 14)  // doc y 0's baseline at scroll 0
#define OPT_EDGE_FADE      14
// Document y range fully inside the window at scroll s: [s - ABOVE, s + BELOW].
#define OPT_VIEW_ABOVE     (OPT_CONTENT_TOP - OPT_DOC_TOP_Y)
#define OPT_VIEW_BELOW     (OPT_DOC_TOP_Y - OPT_CONTENT_BOTTOM)
#define OPT_ROW_H          17   // settings row pitch
#define OPT_SETTINGS_GAP   28   // last settings row -> control row
#define OPT_CREDITS_GAP    30   // last icon row -> first credits line
#define OPT_CREDITS_PITCH  14   // 11 credits lines = 154: the section fits the window
#define OPT_DOC_END_MARGIN 6    // last credits line -> document end
#define OPT_FOCUS_MARGIN   10   // row-follow margin when a section can't fit
#define OPT_ARROW_STEP     60   // one scroll-arrow click

static s32 options_screen_y(s32 docY) {
    return OPT_DOC_TOP_Y - docY + sOptionsScrollPx;
}

// Items sliding out of the window fade with the part that is clipped:
// full alpha while a 14-unit item (baseline/bottom at screen y) is fully
// inside, 0 once it is fully outside.
static s32 options_edge_alpha(s32 y, s32 baseAlpha) {
    s32 in = OPT_CONTENT_TOP - y;
    s32 inBottom = y + OPT_EDGE_FADE - OPT_CONTENT_BOTTOM;
    if (inBottom < in) {
        in = inBottom;
    }
    if (in >= OPT_EDGE_FADE) {
        return baseAlpha;
    }
    if (in <= 0) {
        return 0;
    }
    return baseAlpha * in / OPT_EDGE_FADE;
}

#ifdef MOUSE_ACTIONS
// Pointer hits on document items only count inside the window.
static s32 options_in_window(f32 y) {
    return y >= OPT_CONTENT_BOTTOM && y < OPT_CONTENT_TOP;
}
#endif

// ---------------------------------------------------------------------------
// Objectives grid (the document's middle section): every objective type as
// a 16x16 icon, grouped into labelled bands. A toggles the icon under the
// cursor; A on a band label flips the whole band. A control row above the
// bands (PRESET, TOGGLE ALL). Bands wrap onto extra icon rows when they
// don't fit one (see grid_label_doc).

#define GRID_BAND_COUNT 7

// Band contents and order are Matt's (2026-09-23, arranged with the band sorter page).
static const u8 sGridStars[] = {
    BINGO_OBJECTIVE_STAR,
    BINGO_OBJECTIVE_STARS_IN_LEVEL,
    BINGO_OBJECTIVE_STARS_MULTIPLE_LEVELS,
    BINGO_OBJECTIVE_RED_COIN_STARS,
    BINGO_OBJECTIVE_HUNDRED_COIN_STARS,
    BINGO_OBJECTIVE_CASTLE_SECRET_STARS,
    BINGO_OBJECTIVE_MULTISTAR,
    BINGO_OBJECTIVE_RACING_STARS,
    BINGO_OBJECTIVE_SECRETS_STARS,
};
static const u8 sGridStarChallenges[] = {
    BINGO_OBJECTIVE_STAR_TIMED,
    BINGO_OBJECTIVE_STAR_TTC_RANDOM,
    BINGO_OBJECTIVE_CANNON_STARS,
    BINGO_OBJECTIVE_STAR_COINLESS,
    BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE,
    BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE,
    BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE,
};
// The game's first-class modifiers, in enum BingoModifier order.
static const u8 sGridModifiers[] = {
    BINGO_OBJECTIVE_STAR_GREEN_DEMON,
    BINGO_OBJECTIVE_STAR_REVERSE_JOYSTICK,
    BINGO_OBJECTIVE_RANDOM_RED_COINS,
    BINGO_OBJECTIVE_STAR_CLICK_GAME,
    BINGO_OBJECTIVE_RANDOM_STARS,
    BINGO_OBJECTIVE_STAR_DAREDEVIL,
    BINGO_OBJECTIVE_SPLATOON,
};
static const u8 sGridCollect[] = {
    BINGO_OBJECTIVE_COIN,
    BINGO_OBJECTIVE_MULTICOIN,
    BINGO_OBJECTIVE_RED_COIN,
    BINGO_OBJECTIVE_BLUE_COIN,
    BINGO_OBJECTIVE_1UPS_IN_LEVEL,
    BINGO_OBJECTIVE_LIVES,
    BINGO_OBJECTIVE_SIGNPOST,
    BINGO_OBJECTIVE_POLES,
    BINGO_OBJECTIVE_SHOOT_CANNONS,
    BINGO_OBJECTIVE_WARP_PADS,
    BINGO_OBJECTIVE_KOOPA_SHELLS,
    BINGO_OBJECTIVE_SPIN_HEARTS,
    BINGO_OBJECTIVE_EXCLAMATION_MARK_BOX,
    BINGO_OBJECTIVE_WING_CAP_BOX,
    BINGO_OBJECTIVE_VANISH_CAP_BOX,
    BINGO_OBJECTIVE_METAL_CAP_BOX,
};
static const u8 sGridAntics[] = {
    BINGO_OBJECTIVE_ROOF_WITHOUT_CANNON,
    BINGO_OBJECTIVE_DANGEROUS_WALL_KICKS,
    BINGO_OBJECTIVE_BLJ,
    BINGO_OBJECTIVE_LOSE_MARIO_HAT,
    BINGO_OBJECTIVE_BOWSER,
    BINGO_OBJECTIVE_UNIQUE_DEATHS,
};
static const u8 sGridEnemies[] = {
    BINGO_OBJECTIVE_KILL_GOOMBAS,
    BINGO_OBJECTIVE_KILL_BOBOMBS,
    BINGO_OBJECTIVE_KILL_KOOPAS,
    BINGO_OBJECTIVE_KILL_WHOMPS,
    BINGO_OBJECTIVE_CRUSHED,
    BINGO_OBJECTIVE_HURT_BY_CLAMS,
    BINGO_OBJECTIVE_KILL_MR_BLIZZARDS,
    BINGO_OBJECTIVE_KILL_SPINDRIFTS,
    BINGO_OBJECTIVE_AMPS,
    BINGO_OBJECTIVE_KILL_BOOS,
    BINGO_OBJECTIVE_KILL_MR_IS,
    BINGO_OBJECTIVE_KILL_SCUTTLEBUGS,
    BINGO_OBJECTIVE_KILL_SNUFITS,
    BINGO_OBJECTIVE_KILL_BULLIES,
    BINGO_OBJECTIVE_KILL_CHUCKYAS,
    BINGO_OBJECTIVE_KILL_SKEETERS,
    BINGO_OBJECTIVE_KILL_FLY_GUYS,
};

// Only dealt with "Unlock full game" OFF: while unlock is ON they show as
// OFF and A buzzes (the saved toggle is kept for when unlock goes OFF).
static const u8 sGridProgression[] = {
    BINGO_OBJECTIVE_OPEN_CANNONS,
    BINGO_OBJECTIVE_TOAD_STARS,
    BINGO_OBJECTIVE_MIPS,
};

struct ObjectiveGridBand {
    const char *label;
    const u8 *types;
    u8 count;
};

static const struct ObjectiveGridBand sGridBands[GRID_BAND_COUNT] = {
    { "STARS", sGridStars, ARRAY_COUNT(sGridStars) },
    { "STAR CHALLENGES", sGridStarChallenges, ARRAY_COUNT(sGridStarChallenges) },
    { "BINGO MODIFIERS", sGridModifiers, ARRAY_COUNT(sGridModifiers) },
    { "ANTICS", sGridAntics, ARRAY_COUNT(sGridAntics) },
    { "COLLECTING", sGridCollect, ARRAY_COUNT(sGridCollect) },
    { "ENEMIES", sGridEnemies, ARRAY_COUNT(sGridEnemies) },
    { "PROGRESSION", sGridProgression, ARRAY_COUNT(sGridProgression) },
};

// Every objective must be reachable (the old paged list silently dropped
// the last few): a new BingoObjectiveType fails this until it is placed in
// a band. Uniqueness isn't checkable at compile time; keep the tables in
// sync with the enum by hand.
typedef char grid_bands_cover_every_objective[
    (ARRAY_COUNT(sGridStars) + ARRAY_COUNT(sGridStarChallenges) + ARRAY_COUNT(sGridModifiers)
     + ARRAY_COUNT(sGridCollect) + ARRAY_COUNT(sGridAntics)
     + ARRAY_COUNT(sGridEnemies) + ARRAY_COUNT(sGridProgression)
     == BINGO_OBJECTIVE_TOTAL_AMOUNT) ? 1 : -1];

// Cursor: band index 0..GRID_BAND_COUNT-1; col -1 = the band's label,
// else a linear icon index. Band -1 is the control row above the bands:
// col 0 = PRESET, col 1 = TOGGLE ALL.
static s32 sGridBand = 0;
static s32 sGridCol = 0;

// Layout. x in the menu's units; y as document offsets (units down from
// the control row's baseline). Each band is a label line with its first
// icon row 14 units below and further rows 20 below that; 28-unit
// single-row bands keep the whole section (control row + 5 bands, 160
// units) inside the 168-unit content window.
#define GRID_LEFT_X      22
#define GRID_PITCH       20
#define GRID_MAX_PER_ROW 14
#define GRID_FIRST_DY    16    // control row baseline -> first band label
#define GRID_BAND_H      30    // label row + icon row + breathing room
#define GRID_ICON_DROP   14
#define GRID_FOOTER_Y    28
#define GRID_LABEL_X     24
#define GRID_RIGHT_X     296
#define GRID_FOOTER_NAME_X 44
#define GRID_FOOTER_TOTAL_RIGHT_X 238  // clear of the scroll arrows and BACK
// Footer note (in place of the total) on a progression objective while
// unlock is ON: the generator skips those (bingo_objective_eligible).
#define GRID_GATED_NOTE "Needs Unlock OFF"
#define GRID_CTRL_TOGGLE_X 150
#define GRID_CTRL_GAP    6     // PRESET label -> value

// The control row hides (and can't be selected) while the room's options
// are locked; its space stays, so the layout doesn't jump.
static s32 grid_controls_visible(void) {
#ifndef TARGET_N64
    return !bingo_options_locked();
#else
    return 1;
#endif
}

static s32 grid_band_rows(s32 band) {
    return (sGridBands[band].count + GRID_MAX_PER_ROW - 1) / GRID_MAX_PER_ROW;
}

// Document y of the settings rows, the control row and the credits (the
// settings row count varies with the online rows, so all of these are
// computed on use; they're cheap).
static s32 options_settings_doc(s32 row) {
    return OPT_ROW_H * row;
}

static s32 grid_ctrl_doc(void) {
    return options_settings_doc(BINGO_CONFIGS_IN_LEFT_COL - 1) + OPT_SETTINGS_GAP;
}

// Bands flow top to bottom, taller by 20 per extra icon row.
static s32 grid_label_doc(s32 band) {
    s32 b, y = grid_ctrl_doc() + GRID_FIRST_DY;
    for (b = 0; b < band; b++) {
        y += GRID_BAND_H + GRID_PITCH * (grid_band_rows(b) - 1);
    }
    return y;
}

// The bottom of the last icon row's highlight box.
static s32 grid_bottom_doc(void) {
    s32 last = GRID_BAND_COUNT - 1;
    return grid_label_doc(last) + GRID_ICON_DROP + GRID_PITCH * (grid_band_rows(last) - 1) + 2;
}

// The control row is a sticky section header: at its natural place until
// that scrolls above the window's first baseline, then pinned there, until
// the section's end (the last icon box's bottom) pushes it off upward.
// Every draw / highlight / hit test of the row goes through here.
static s32 grid_ctrl_pinned(void) {
    return options_screen_y(grid_ctrl_doc()) > OPT_DOC_TOP_Y;
}

static s32 grid_ctrl_y(void) {
    s32 natural = options_screen_y(grid_ctrl_doc());
    s32 pushed = options_screen_y(grid_bottom_doc()) + 4;
    if (natural <= OPT_DOC_TOP_Y) {
        return natural;
    }
    return pushed > OPT_DOC_TOP_Y ? pushed : OPT_DOC_TOP_Y;
}

// The pinned row's dark backing, which the bands scroll under.
#define GRID_CTRL_BACK_X0 20
#define GRID_CTRL_BACK_X1 300

static s32 grid_label_y(s32 band) {
    return options_screen_y(grid_label_doc(band));
}

// Icon i of a band: row i / GRID_MAX_PER_ROW, column i % GRID_MAX_PER_ROW.
static s32 grid_icon_x(s32 i) {
    return GRID_LEFT_X + GRID_PITCH * (i % GRID_MAX_PER_ROW);
}

static s32 grid_icon_y(s32 band, s32 i) {
    return grid_label_y(band) - GRID_ICON_DROP - GRID_PITCH * (i / GRID_MAX_PER_ROW);
}

// Keep the grid cursor valid (the control row can hide under it).
static void grid_clamp_cursor(void) {
    if (sGridBand == -1 && grid_controls_visible()) {
        if (sGridCol < 0) {
            sGridCol = 0;
        } else if (sGridCol > 1) {
            sGridCol = 1;
        }
        return;
    }
    if (sGridBand < 0 || sGridBand >= GRID_BAND_COUNT) {
        sGridBand = 0;
        sGridCol = 0;
    }
    if (sGridCol >= sGridBands[sGridBand].count) {
        sGridCol = sGridBands[sGridBand].count - 1;
    }
}

// A progression objective while unlock is ON: the generator won't deal it,
// so the grid shows it OFF and A won't toggle it.
static s32 grid_objective_gated(u8 type) {
    return gBingoFullGameUnlocked && bingo_objective_needs_unlock_off(type);
}

static s32 grid_objective_on(u8 type) {
    return !gBingoObjectivesDisabled[type] && !grid_objective_gated(type);
}

static s32 grid_band_gated(s32 band) {
    return grid_objective_gated(sGridBands[band].types[0]);
}

static s32 grid_band_enabled(s32 band) {
    s32 i, n = 0;
    for (i = 0; i < sGridBands[band].count; i++) {
        n += grid_objective_on(sGridBands[band].types[i]);
    }
    return n;
}

// Tiny formatter for the grid's runtime strings (no sprintf on N64).
static char *grid_append(char *dst, const char *src) {
    while (*src != '\0') {
        *dst++ = *src++;
    }
    *dst = '\0';
    return dst;
}

static char *grid_append_num(char *dst, s32 n) {
    char tmp[8];
    s32 len = 0;
    do {
        tmp[len++] = '0' + n % 10;
        n /= 10;
    } while (n > 0);
    while (len > 0) {
        *dst++ = tmp[--len];
    }
    *dst = '\0';
    return dst;
}

// "12/14".
static void grid_count_text(char *out, s32 n, s32 total) {
    grid_append_num(grid_append(grid_append_num(out, n), "/"), total);
}

// Bottom-up menu rect [x0,x1) x [y0,y1) -> FillRectangle's top-down coords.
static void grid_fill_rect(s32 x0, s32 y0, s32 x1, s32 y1) {
    gDPFillRectangle(gDisplayListHead++, x0, SCREEN_HEIGHT - y1, x1,
                     SCREEN_HEIGHT - y0);
}

// The US generic font has no slash: main_font_lut's "slash" (0x9F) is
// really the hyphen, and print_generic_string prints 0xD0 as a blank
// double space. So grid strings draw '/' themselves, as a 1-unit
// stair-step over the digits' height (glyph rows 3..12) in the current
// text colour, which matches the font's 1-texel strokes. Set that colour
// with grid_text_color (the rects can't read back the env colour).
#define GRID_SLASH_W 7
static u8 sGridTextColor[4] = { 255, 255, 255, 255 };

static void grid_text_color(u8 r, u8 g, u8 b, u8 a) {
    sGridTextColor[0] = r;
    sGridTextColor[1] = g;
    sGridTextColor[2] = b;
    sGridTextColor[3] = a;
    gDPSetEnvColor(gDisplayListHead++, r, g, b, a);
}

static void grid_print_slash(s32 x, s32 y) {
    s32 row;
    gDPPipeSync(gDisplayListHead++);
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gDisplayListHead++, 0, 0, sGridTextColor[0], sGridTextColor[1],
                    sGridTextColor[2], sGridTextColor[3]);
    for (row = 3; row <= 12; row++) {
        s32 px = x + (row - 3) * 6 / 10;
        grid_fill_rect(px, y + row, px + 1, y + row + 1);
    }
    // Back to text mode (dl_ia_text_begin resets the env colour).
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, sGridTextColor[0], sGridTextColor[1],
                   sGridTextColor[2], sGridTextColor[3]);
}

// ASCII -> generic-font charmap, for the grid's computed strings (counts
// can't live in _() literals), up to the next '/' or the end. Unmapped
// characters become spaces. Returns the ASCII characters consumed.
static s32 grid_ascii_to_menu(u8 *dst, const char *src, s32 dstSize) {
    s32 i;
    for (i = 0; src[i] != '\0' && src[i] != '/' && i < dstSize - 1; i++) {
        char c = src[i];
        u8 out = 0x9E;  // space
        if (c >= '0' && c <= '9') {
            out = c - '0';
        } else if (c >= 'A' && c <= 'Z') {
            out = 0x0A + (c - 'A');
        } else if (c >= 'a' && c <= 'z') {
            out = 0x24 + (c - 'a');
        } else if (c == ':') {
            out = 0xE6;
        } else if (c == '.') {
            out = 0x3F;
        } else if (c == ',') {
            out = 0x6F;
        }
        dst[i] = out;
    }
    dst[i] = 0xFF;
    return i;
}

static s32 grid_ascii_width(const char *str) {
    u8 buf[48];
    s32 w = 0;
    while (*str != '\0') {
        if (*str == '/') {
            w += GRID_SLASH_W;
            str++;
        } else {
            str += grid_ascii_to_menu(buf, str, sizeof(buf));
            w += get_string_width(buf);
        }
    }
    return w;
}

static void grid_print_ascii(s32 x, s32 y, const char *str) {
    u8 buf[48];
    while (*str != '\0') {
        if (*str == '/') {
            grid_print_slash(x, y);
            x += GRID_SLASH_W;
            str++;
        } else {
            str += grid_ascii_to_menu(buf, str, sizeof(buf));
            print_generic_string(x, y, buf);
            x += get_string_width(buf);
        }
    }
}

static s32 grid_band_label_width(s32 band) {
    return grid_ascii_width(sGridBands[band].label);
}

// Presets stamp a named loadout; the current one is recomputed from state,
// so Custom (after hand edits) cycles to the first preset.
static void bingo_preset_cycle(void) {
    bingo_preset_apply((bingo_preset_current() + 1) % BINGO_PRESET_COUNT);
}

// The PRESET control's value, matched from the current state.
static const char *grid_preset_name(void) {
    switch (bingo_preset_current()) {
        case BINGO_PRESET_SRL:     return "SRL";
        case BINGO_PRESET_VANILLA: return "Vanilla";
        case BINGO_PRESET_CASUAL:  return "Casual";
        default:                   return "Custom";
    }
}

// Control-row geometry: col 0 = PRESET (label + value), col 1 = TOGGLE
// ALL. The label's x, and where the control's text ends.
static s32 grid_control_x(s32 col) {
    return col == 0 ? GRID_LABEL_X : GRID_CTRL_TOGGLE_X;
}

static s32 grid_control_end_x(s32 col) {
    if (col == 0) {
        return GRID_LABEL_X + grid_ascii_width("PRESET") + GRID_CTRL_GAP
               + grid_ascii_width(grid_preset_name());
    }
    return GRID_CTRL_TOGGLE_X + grid_ascii_width("TOGGLE ALL");
}

// Mixed or all-off turns everything on; only a fully-on pool clears
// (the band labels' rule, pool-wide). Gated objectives are skipped: they
// read as OFF and keep their saved toggle.
static void grid_toggle_all(void) {
    s32 i;
    u8 disable = 1;
    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
        if (!grid_objective_gated(i) && gBingoObjectivesDisabled[i]) {
            disable = 0;
        }
    }
    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
        if (!grid_objective_gated(i)) {
            gBingoObjectivesDisabled[i] = disable;
        }
    }
}

// ---------------------------------------------------------------------------
// Document extent, focus-driven scrolling and the pinned footer chrome.

#define OPT_CREDITS_LINES 11
// Settings rows' highlight / hit box, x (the printer's LEFT_X..RIGHT_X).
#define OPT_SETTINGS_X0   24
#define OPT_SETTINGS_X1   160
// Footer strip chrome: BACK right-aligned at the strip's end, and the
// scroll arrows (pointing up above down) between the grid total and BACK.
#define OPT_BACK_RIGHT_X  296
#define OPT_ARROW_X       252   // the arrows' centre column
#define OPT_ARROW_UP_Y    37    // up arrow: rows 37..41
#define OPT_ARROW_DOWN_Y  29    // down arrow: rows 29..33

static s32 options_credits_doc(void) {
    return grid_bottom_doc() + OPT_CREDITS_GAP;
}

// The last credits line's baseline.
static s32 options_credits_last_doc(void) {
    return options_credits_doc() + OPT_CREDITS_PITCH * (OPT_CREDITS_LINES - 1);
}

static s32 options_max_scroll(void) {
    s32 bottom = options_credits_last_doc() + OPT_DOC_END_MARGIN;
    s32 m = bottom - OPT_VIEW_BELOW;
    return m > 0 ? m : 0;
}

static s32 options_clamp_scroll(s32 s) {
    s32 max = options_max_scroll();
    return s < 0 ? 0 : (s > max ? max : s);
}

// Move the scroll target the least that shows document rows [top, bot].
static void options_reveal(s32 top, s32 bot) {
    s32 t = sOptionsScrollTarget;
    if (bot > t + OPT_VIEW_BELOW) {
        t = bot - OPT_VIEW_BELOW;
    }
    if (top < t - OPT_VIEW_ABOVE) {
        t = top + OPT_VIEW_ABOVE;
    }
    sOptionsScrollTarget = options_clamp_scroll(t);
}

// The view follows the focus: the focused item's whole section when it
// fits the window, else the focused row plus a margin. The credits (which
// fit) are shown from their top, at the window's top (clamped).
static void options_scroll_to_focus(void) {
    s32 top, bot, rowBot;
    if (sOptionsFocus == OPTIONS_FOCUS_CREDITS) {
        sOptionsScrollTarget =
            options_clamp_scroll(options_credits_doc() - 16 + OPT_VIEW_ABOVE);
        options_reveal(options_credits_doc() - 16, options_credits_last_doc() + 2);
        return;
    }
    if (sOptionsFocus == OPTIONS_FOCUS_SETTINGS) {
        top = options_settings_doc(0) - 16;
        bot = options_settings_doc(BINGO_CONFIGS_IN_LEFT_COL - 1) + 2;
        rowBot = options_settings_doc(sBingoOptionSelection) + 2;
    } else {
        if (sGridBand < 0) {
            // The sticky control row is visible whenever the section is;
            // only when the section has scrolled away entirely (the row is
            // pushed off above the window, e.g. L from the credits) bring
            // the section's top back.
            if (grid_ctrl_y() > OPT_DOC_TOP_Y) {
                options_reveal(grid_ctrl_doc() - 16, grid_ctrl_doc() + 2);
            }
            return;
        }
        // Keep the focused band row clear of the sticky header's 20 units.
        top = grid_ctrl_doc() - 16 - 20;
        bot = grid_bottom_doc();
        if (sGridCol < 0) {
            rowBot = grid_label_doc(sGridBand) + 2;
        } else {
            rowBot = grid_label_doc(sGridBand) + GRID_ICON_DROP
                     + GRID_PITCH * (sGridCol / GRID_MAX_PER_ROW) + 2;
        }
    }
    if (bot - top <= OPT_VIEW_ABOVE + OPT_VIEW_BELOW) {
        options_reveal(top, bot);
    } else {
        s32 rowTop = rowBot - 18 - OPT_FOCUS_MARGIN;
        if (sOptionsFocus == OPTIONS_FOCUS_GRID) {
            rowTop -= 20;  // below the sticky header
        }
        options_reveal(rowTop, rowBot + OPT_FOCUS_MARGIN);
    }
}

// Ease toward the target; once per logic frame (the PC port can draw
// twice per frame, so this lives in the input path, gated on the timer).
static void options_update_scroll(void) {
    static u32 lastTimer = 0xFFFFFFFF;
    f32 d;
    sOptionsScrollTarget = options_clamp_scroll(sOptionsScrollTarget);
    if (gGlobalTimer != lastTimer) {
        lastTimer = gGlobalTimer;
        d = (f32) sOptionsScrollTarget - sOptionsScroll;
        if (d < 0.5f && d > -0.5f) {
            sOptionsScroll = (f32) sOptionsScrollTarget;
        } else {
            sOptionsScroll += d * 0.35f;
        }
    }
    sOptionsScrollPx = (s32) (sOptionsScroll + 0.5f);
}

#ifndef TARGET_N64
static s32 options_back_x(void) {
    return OPT_BACK_RIGHT_X - grid_ascii_width("BACK");
}

// The clickable BACK tag, right end of the footer strip (the hand can't
// reach above row 210, where it used to sit).
static s32 options_back_tag_hovered(void) {
    f32 x = sCursorPos[0] + 160.0f;
    f32 y = sCursorPos[1] + 120.0f;
    return x >= options_back_x() - 3 && x < OPT_BACK_RIGHT_X + 3
           && y >= GRID_FOOTER_Y && y < GRID_FOOTER_Y + 17;
}
#endif

static void options_focus_grid_start(void) {
    sOptionsFocus = OPTIONS_FOCUS_GRID;
    sGridBand = grid_controls_visible() ? -1 : 0;
    sGridCol = 0;
}

static void options_focus_settings_row(s32 row) {
    sOptionsFocus = OPTIONS_FOCUS_SETTINGS;
    sBingoOptionSelection = row;
}

#ifdef MOUSE_ACTIONS
// What's under the pointer in the grid section (menu coords, bottom-up).
// Returns 0 on empty space or outside the content window.
static s32 grid_hit_test(f32 x, f32 y, s32 *band, s32 *col) {
    s32 b, r, c, cy = grid_ctrl_y();
    if (!options_in_window(y)) {
        return 0;
    }
    // The control row first (it overlays the bands while pinned): same
    // box as a band label's.
    if (grid_controls_visible() && y >= cy + 2 && y < cy + 16) {
        for (c = 0; c < 2; c++) {
            if (x >= grid_control_x(c) - 2 && x < grid_control_end_x(c) + 2) {
                *band = -1;
                *col = c;
                return 1;
            }
        }
    }
    // The rest of the pinned row's backing hides what's under it.
    if (grid_controls_visible() && grid_ctrl_pinned() && y >= cy - 3 && y < cy + 17
        && x >= GRID_CTRL_BACK_X0 && x < GRID_CTRL_BACK_X1) {
        return 0;
    }
    for (b = 0; b < GRID_BAND_COUNT; b++) {
        s32 ly = grid_label_y(b);
        if (y >= ly + 2 && y < ly + 16 && x >= GRID_LEFT_X
            && x < GRID_LABEL_X + grid_band_label_width(b) + 2) {
            *band = b;
            *col = -1;
            return 1;
        }
        for (r = 0; r < grid_band_rows(b); r++) {
            s32 iy = ly - GRID_ICON_DROP - GRID_PITCH * r;
            if (y >= iy - 2 && y < iy + 18 && x >= GRID_LEFT_X - 2) {
                c = ((s32) x - (GRID_LEFT_X - 2)) / GRID_PITCH;
                if (c < GRID_MAX_PER_ROW
                    && r * GRID_MAX_PER_ROW + c < sGridBands[b].count) {
                    *band = b;
                    *col = r * GRID_MAX_PER_ROW + c;
                    return 1;
                }
            }
        }
    }
    return 0;
}

// The settings row under the pointer (its highlight box).
static s32 settings_hit_test(f32 x, f32 y, s32 *row) {
    s32 i;
    if (!options_in_window(y) || x < OPT_SETTINGS_X0 || x >= OPT_SETTINGS_X1) {
        return 0;
    }
    for (i = 0; i < BINGO_CONFIGS_IN_LEFT_COL; i++) {
        s32 ry = options_screen_y(options_settings_doc(i));
        if (y >= ry && y < ry + 16) {
            *row = i;
            return 1;
        }
    }
    return 0;
}

// +1 = the up arrow (scroll toward the top), -1 = the down arrow, 0 = none.
// Boxes are 14 x 8, wider and taller than the 9 x 5 glyphs.
static s32 options_arrow_hit(f32 x, f32 y) {
    if (x < OPT_ARROW_X - 7 || x >= OPT_ARROW_X + 7) {
        return 0;
    }
    if (y >= OPT_ARROW_UP_Y - 1 && y < OPT_ARROW_UP_Y + 7) {
        return 1;
    }
    if (y >= OPT_ARROW_DOWN_Y - 1 && y < OPT_ARROW_DOWN_Y + 7) {
        return -1;
    }
    return 0;
}
#endif

static void grid_toggle_band(s32 band) {
    s32 i;
    // Mixed or all-off turns everything on; only a fully-on band clears.
    u8 disable = grid_band_enabled(band) == sGridBands[band].count;
    for (i = 0; i < sGridBands[band].count; i++) {
        gBingoObjectivesDisabled[sGridBands[band].types[i]] = disable;
    }
}

// A on the grid. Applied here in the (once-per-logic-frame) input handler,
// not in the printer, which can run twice per frame on PC.
static void grid_activate(void) {
#ifdef MOUSE_ACTIONS
    // A mouse click acts on what's under the pointer; a click on empty
    // space must not toggle whatever the keyboard cursor last selected.
    if (mouse_window_buttons & 1) {
        s32 band, col;
        if (!grid_hit_test(sCursorPos[0] + 160.0f, sCursorPos[1] + 120.0f,
                           &band, &col)) {
            return;
        }
        sGridBand = band;
        sGridCol = col;
    }
#endif
#ifndef TARGET_N64
    if (bingo_options_locked()) {
        play_sound(SOUND_MENU_CAMERA_BUZZ, gGlobalSoundSource);
        return;
    }
#endif
    if (sGridBand < 0) {
        if (sGridCol == 0) {
            bingo_preset_cycle();
        } else {
            grid_toggle_all();
        }
    } else if (grid_band_gated(sGridBand)) {
        play_sound(SOUND_MENU_CAMERA_BUZZ, gGlobalSoundSource);
    } else if (sGridCol < 0) {
        grid_toggle_band(sGridBand);
    } else {
        gBingoObjectivesDisabled[sGridBands[sGridBand].types[sGridCol]] ^= 1;
    }
}

// A on a settings row.
static void settings_activate(void) {
#ifndef TARGET_N64
    if (bingo_options_locked()) {
        play_sound(SOUND_MENU_CAMERA_BUZZ, gGlobalSoundSource);
        return;
    }
#endif
    // Applied by the row's printer on the next draw.
    sToggleCurrentOption = 1;
}

// D-pad / C navigation in the grid; returns 1 when the focus moved.
// Left/right walk the band's icons linearly (across its wrapped rows) and
// the label, or flip between the control row's two controls. Up from the
// control row leaves for the settings rows; down from the last band for
// the credits.
static s32 grid_navigate(u16 pressed) {
    s32 last = GRID_BAND_COUNT - 1;
    if (sGridBand < 0) {
        if (pressed & (D_JPAD | D_CBUTTONS)) {
            sGridBand = 0;
            sGridCol = 0;
        } else if (pressed & (U_JPAD | U_CBUTTONS)) {
            options_focus_settings_row(BINGO_CONFIGS_IN_LEFT_COL - 1);
        } else if (pressed & (R_JPAD | R_CBUTTONS | L_JPAD | L_CBUTTONS)) {
            sGridCol = !sGridCol;
        } else {
            return 0;
        }
        return 1;
    }
    // Up/down step through a band's icon rows before leaving it; the
    // column is kept, clamped to a shorter row's last icon. The label
    // (col -1) moves label to label.
    if (pressed & (D_JPAD | D_CBUTTONS)) {
        if (sGridCol >= 0 && sGridCol + GRID_MAX_PER_ROW
                                 < grid_band_rows(sGridBand) * GRID_MAX_PER_ROW) {
            sGridCol += GRID_MAX_PER_ROW;
        } else {
            if (sGridBand >= last) {
                sOptionsFocus = OPTIONS_FOCUS_CREDITS;
                return 1;
            }
            sGridBand++;
            if (sGridCol >= 0) {
                sGridCol %= GRID_MAX_PER_ROW;
            }
        }
    } else if (pressed & (U_JPAD | U_CBUTTONS)) {
        if (sGridCol >= GRID_MAX_PER_ROW) {
            sGridCol -= GRID_MAX_PER_ROW;
        } else {
            if (sGridBand <= 0) {
                if (grid_controls_visible()) {
                    sGridBand = -1;
                    sGridCol = 0;
                } else {
                    options_focus_settings_row(BINGO_CONFIGS_IN_LEFT_COL - 1);
                }
                return 1;
            }
            sGridBand--;
            if (sGridCol >= 0) {
                sGridCol = (grid_band_rows(sGridBand) - 1) * GRID_MAX_PER_ROW
                           + sGridCol % GRID_MAX_PER_ROW;
            }
        }
    } else if (pressed & (R_JPAD | R_CBUTTONS)) {
        sGridCol = sGridCol + 1 >= sGridBands[sGridBand].count ? -1 : sGridCol + 1;
    } else if (pressed & (L_JPAD | L_CBUTTONS)) {
        sGridCol = sGridCol <= -1 ? sGridBands[sGridBand].count - 1 : sGridCol - 1;
    } else {
        return 0;
    }
    if (sGridCol >= sGridBands[sGridBand].count) {
        sGridCol = sGridBands[sGridBand].count - 1;
    }
    return 1;
}

// The settings rows: up/down, no wrap; down from the last row enters the
// grid at its control row.
static s32 settings_navigate(u16 pressed) {
    if (pressed & (D_JPAD | D_CBUTTONS)) {
        if (sBingoOptionSelection + 1 >= BINGO_CONFIGS_IN_LEFT_COL) {
            options_focus_grid_start();
        } else {
            sBingoOptionSelection++;
        }
    } else if (pressed & (U_JPAD | U_CBUTTONS)) {
        if (sBingoOptionSelection <= 0) {
            return 0;
        }
        sBingoOptionSelection--;
    } else {
        return 0;
    }
    return 1;
}

// The credits: nothing to select; up returns to the last band.
static s32 credits_navigate(u16 pressed) {
    if (pressed & (U_JPAD | U_CBUTTONS)) {
        sOptionsFocus = OPTIONS_FOCUS_GRID;
        sGridBand = GRID_BAND_COUNT - 1;
        grid_clamp_cursor();
        return 1;
    }
    return 0;
}

#ifdef MOUSE_ACTIONS
// Hover selects, but only when the pointer actually moved: a resting
// pointer must not snap the selection back after d-pad moves (or when the
// document scrolls under it).
// Hover only follows pointer movement made on this screen: the hand is
// still parked over the OPTIONS button when the screen opens, and that
// spot lies over the grid, so the first frame must not steal the focus
// from Game mode.

static void options_mouse_hover(void) {
    s32 band, col, row;
    if (!sOptionsHoverArmed) {
        sOptionsHoverArmed = 1;
        sOptionsHoverLastX = sCursorPos[0];
        sOptionsHoverLastY = sCursorPos[1];
        return;
    }
    if (sCursorPos[0] == sOptionsHoverLastX && sCursorPos[1] == sOptionsHoverLastY) {
        return;
    }
    sOptionsHoverLastX = sCursorPos[0];
    sOptionsHoverLastY = sCursorPos[1];
    f32 lastX = sOptionsHoverLastX, lastY = sOptionsHoverLastY;
    if (grid_hit_test(lastX + 160.0f, lastY + 120.0f, &band, &col)) {
        sOptionsFocus = OPTIONS_FOCUS_GRID;
        sGridBand = band;
        sGridCol = col;
    } else if (settings_hit_test(lastX + 160.0f, lastY + 120.0f, &row)) {
        options_focus_settings_row(row);
    }
}
#endif

// A: a mouse click acts on what's under the pointer (a scroll arrow
// scrolls without moving the focus; empty space does nothing); a button
// press acts on the focus.
static void options_activate(void) {
#ifdef MOUSE_ACTIONS
    if (mouse_window_buttons & 1) {
        f32 x = sCursorPos[0] + 160.0f;
        f32 y = sCursorPos[1] + 120.0f;
        s32 band, col, row, arrow = options_arrow_hit(x, y);
        if (arrow != 0) {
            sOptionsScrollTarget =
                options_clamp_scroll(sOptionsScrollTarget - arrow * OPT_ARROW_STEP);
        } else if (settings_hit_test(x, y, &row)) {
            options_focus_settings_row(row);
            settings_activate();
        } else if (grid_hit_test(x, y, &band, &col)) {
            sOptionsFocus = OPTIONS_FOCUS_GRID;
            grid_activate();
        }
        return;
    }
#endif
    if (sOptionsFocus == OPTIONS_FOCUS_GRID) {
        grid_activate();
    } else if (sOptionsFocus == OPTIONS_FOCUS_SETTINGS) {
        settings_activate();
    }
}

/**
 * Cursor function that handles button inputs.
 * If the cursor is clicked, sClickPos uses the same value as sCursorPos.
 */
static void handle_cursor_button_input(void) {
    if (sSelectedButtonID == MENU_BUTTON_SEED_OPTION) {
        u16 pressed = gPlayer3Controller->buttonPressed;
        options_update_scroll();
        if (pressed & (B_BUTTON | START_BUTTON)) {
            options_screen_exit();
        } else {
            s32 moved = 0;
            // The online Opp. rows can vanish under the cursor.
            if (sBingoOptionSelection >= BINGO_CONFIGS_IN_LEFT_COL) {
                sBingoOptionSelection = BINGO_CONFIGS_IN_LEFT_COL - 1;
            }
            grid_clamp_cursor();
#ifdef MOUSE_ACTIONS
            options_mouse_hover();
#endif
            if (sBingoOptionSelectTimer > 0) {
                sBingoOptionSelectTimer--;
            } else if (pressed & R_TRIG) {
                // Jump to the next section's start (no wrap).
                if (sOptionsFocus == OPTIONS_FOCUS_SETTINGS) {
                    options_focus_grid_start();
                    moved = 1;
                } else if (sOptionsFocus == OPTIONS_FOCUS_GRID) {
                    sOptionsFocus = OPTIONS_FOCUS_CREDITS;
                    moved = 1;
                }
            } else if (pressed & L_TRIG) {
                if (sOptionsFocus == OPTIONS_FOCUS_CREDITS) {
                    options_focus_grid_start();
                    moved = 1;
                } else if (sOptionsFocus == OPTIONS_FOCUS_GRID) {
                    options_focus_settings_row(0);
                    moved = 1;
                }
            } else if (sOptionsFocus == OPTIONS_FOCUS_SETTINGS) {
                moved = settings_navigate(pressed);
            } else if (sOptionsFocus == OPTIONS_FOCUS_GRID) {
                moved = grid_navigate(pressed);
            } else {
                moved = credits_navigate(pressed);
            }
            if (moved) {
                sBingoOptionSelectTimer = BINGO_OPTION_TIMER_FRAMES;
                options_scroll_to_focus();
            }
            if (pressed & A_BUTTON) {
#ifndef TARGET_N64
                if (options_back_tag_hovered()) {
                    // Clicked the BACK tag: same as pressing B.
                    options_screen_exit();
                } else
#endif
                {
                    options_activate();
                }
            }
        }
#ifndef TARGET_N64
    } else if (sSelectedButtonID == MENU_BUTTON_ONLINE) {
        s32 request = online_lobby_handle_input(sCursorPos[0] + 160.0f,
                                                sCursorPos[1] + 120.0f);
        if (request == 1) {
            // Leave the lobby screen (any connection stays up).
            sTextBaseAlpha = 0;
            sLobbyExitRequest = 1;
        } else if (request == -1) {
            // A click away from the field rows: maybe one of the lobby's
            // 3D buttons (lobby_screen_loop checks).
            sClickPos[0] = sCursorPos[0];
            sClickPos[1] = sCursorPos[1];
            sCursorClickingTimer = 1;
        }
    } else if (sSelectedButtonID == MENU_BUTTON_PLAY_FILE_A) {
        // The 1P setup screen.
        if (gSeedTypingActive) {
            // The keyboard owns input while the seed is being typed.
            if (text_input_take_finished()) {
                seed_typing_finish();
            }
            return;
        }
        if (gPlayer3Controller->buttonPressed & B_BUTTON) {
            sTextBaseAlpha = 0;
            s1PExitRequest = 1;
        } else if (gPlayer3Controller->buttonPressed & START_BUTTON) {
            // ENTER/START means "go", same as clicking the START button.
            // (It used to back out of the screen, which read as broken.)
            struct Object *door = sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A];
            if (door->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN
                && !s1PExitRequest) {
                play_sound(SOUND_MENU_STAR_SOUND_OKEY_DOKEY, gGlobalSoundSource);
                delete_submenu_button(MENU_BUTTON_1P_START);
                delete_submenu_button(MENU_BUTTON_1P_OPTIONS);
                load_main_menu_save_file(door, 1);
            }
        } else if (gPlayer1Controller->buttonPressed & Z_BUTTON_DEF(A_BUTTON)) {
            sClickPos[0] = sCursorPos[0];
            sClickPos[1] = sCursorPos[1];
            sCursorClickingTimer = 1;
        }
#endif
    } else { // If cursor is clicked
        if (gPlayer1Controller->buttonPressed & Z_BUTTON_DEF(A_BUTTON | B_BUTTON | START_BUTTON)) {
            sClickPos[0] = sCursorPos[0];
            sClickPos[1] = sCursorPos[1];
            sCursorClickingTimer = 1;
        }
    }
}

/**
 * Cursor function that handles analog stick input and button presses with a function near the end.
 */
void handle_controller_cursor_input(void) {
    s16 rawStickX = gPlayer1Controller->rawStickX;
    s16 rawStickY = gPlayer1Controller->rawStickY;

#ifdef MOUSE_ACTIONS 
    controller_mouse_read_window();
#endif

    // Handle deadzone
    if (rawStickY > -2 && rawStickY < 2) {
        rawStickY = 0;
    }
    #ifdef MOUSE_ACTIONS 
    else {
        mouse_has_current_control = FALSE;
    }
    #endif
    if (rawStickX > -2 && rawStickX < 2) {
        rawStickX = 0;
    }
    #ifdef MOUSE_ACTIONS 
    else {
        mouse_has_current_control = FALSE;
    }
    #endif

    // Move cursor
    sCursorPos[0] += rawStickX / 8;
    sCursorPos[1] += rawStickY / 8;

#ifdef MOUSE_ACTIONS
    float screenScale = (float) gfx_current_dimensions.height / SCREEN_HEIGHT;
    f32 mousePosX = (((mouse_window_x - (gfx_current_dimensions.width - (screenScale * 320)) / 2) / screenScale) - 160.0f);
    f32 mousePosY = ((mouse_window_y / screenScale - 120.0f) * -1);
if (!controller_mouse_set_position(&sCursorPos[0], &sCursorPos[1], mousePosX, mousePosY, sSelectedFileNum == 0, FALSE))
#endif
    {
    // Stop cursor from going offscreen
    if (sCursorPos[0] > GFX_DIMENSIONS_FROM_RIGHT_EDGE(188.0f)) {
        sCursorPos[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(188.0f);
    }
    if (sCursorPos[0] < GFX_DIMENSIONS_FROM_LEFT_EDGE(-132.0f)) {
        sCursorPos[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(-132.0f);
    }

    if (sCursorPos[1] > 90.0f) {
        sCursorPos[1] = 90.0f;
    }
    if (sCursorPos[1] < -90.0f) {
        sCursorPos[1] = -90.0f;
    }
    }

    if (sCursorClickingTimer == 0) {
        handle_cursor_button_input();
    }
}

/**
 * Prints the cursor (Mario Hand, different to the one in the Mario screen)
 * and loads it's controller inputs in handle_controller_cursor_input
 * to be usable on the file select.
 */
void print_menu_cursor(void) {
    handle_controller_cursor_input();
    create_dl_translation_matrix(MENU_MTX_PUSH, sCursorPos[0] + 160.0f - 5.0, sCursorPos[1] + 120.0f - 25.0, 0.0f);
    // Get the right graphic to use for the cursor.
    if (sCursorClickingTimer == 0) {
        // Idle
        gSPDisplayList(gDisplayListHead++, dl_menu_idle_hand);
    }
    if (sCursorClickingTimer != 0) {
        // Grabbing
        gSPDisplayList(gDisplayListHead++, dl_menu_grabbing_hand);
    }
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
    if (sCursorClickingTimer != 0) {
        sCursorClickingTimer++; // This is a very strange way to implement a timer? It counts up and
                                // then resets to 0 instead of just counting down to 0.
        if (sCursorClickingTimer == 5) {
            sCursorClickingTimer = 0;
        }
    }
}

/**
 * Prints a hud string depending of the hud table list defined with text fade properties.
 */
void print_hud_lut_string_fade(s8 hudLUT, s16 x, s16 y, const u8 *text) {
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, sTextBaseAlpha - sTextFadeAlpha);
    print_hud_lut_string(hudLUT, x, y, text);
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_end);
}

/**
 * Prints a generic white string with text fade properties.
 */
void print_generic_string_fade(s16 x, s16 y, const u8 *text) {
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, sTextBaseAlpha - sTextFadeAlpha);
    print_generic_string(x, y, text);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

#ifdef TARGET_N64
static void draw_seed_mode_menu(void) {
    s32 xSeedPos;
    unsigned char textEnterSeed[] = { TEXT_ENTER_SEED };
    // Display "ENTER SEED" text
    print_hud_lut_string_fade(2, 100, 35, textEnterSeed);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);

    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, sTextBaseAlpha);
    print_generic_string(47, 34, textReset);
    print_generic_string(35, 104, textBackspace);
    print_generic_string(241, 34, textOption);
    print_generic_string(245, 104, textStart);

    // Display seed
    if (gBingoSeedIsSet) {
        xSeedPos = 105;
    } else {
        xSeedPos = 125;
    }
    print_hud_lut_string_fade(2, xSeedPos, 100 + (30 * -1), gBingoSeedText);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}
#else
// The PC main screen: just the two doors (1P and NET) under the title.
static void draw_main_menu_pc(void) {
    static const u8 textBingo64Hud[] =
        { 0x0B, 0x12, 0x17, 0x10, 0x18, GLOBAL_CHAR_SPACE, 0x06, 0x04, 0xFF };  // "BINGO 64"
    print_hud_lut_string_fade(2, 104, 35, textBingo64Hud);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, sTextBaseAlpha);
    // Centered under the doors (world x -4600/0/+4600 -> screen 89/160/231).
    net_print_ascii_centered(89, 101, "1 PLAYER");
    net_print_ascii_centered(160, 101, "ONLINE");
    net_print_ascii_centered(231, 101, "SETTINGS");
    // Two lines of what-is-this for the first-time player, quiet, at the
    // bottom of the room. Instructions, not decoration.
    // Wording still being workshopped (Matt, 2026-08-19) — kept out of
    // playtest builds until it lands.
    // gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, MIN(sTextBaseAlpha, 140));
    // net_print_ascii_centered(160, 40, "EVERY SEED DEALS A CARD OF 25 GOALS.");
    // net_print_ascii_centered(160, 28, "FIVE IN A ROW IS BINGO. GO FAST.");
    // gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, sTextBaseAlpha);
    if (network_active()) {
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 140, MIN(sTextBaseAlpha, 200));
        net_print_ascii(100, 62, "IN AN ONLINE ROOM");
    }
    // Version stamp: with no-back-compat netplay, "which build am I on"
    // must be answerable from a screenshot.
    {
        char ver[12];
        snprintf(ver, sizeof(ver), "V%d", NET_PROTOCOL_VERSION);
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, MIN(sTextBaseAlpha, 120));
        net_print_ascii((s16) GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(30), 10, ver);
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

// The 1P setup screen: seed (click to type) plus START / OPTIONS buttons.
static void draw_1p_setup(void) {
    s32 xSeedPos;
    unsigned char textEnterSeed[] = { TEXT_ENTER_SEED };
    static unsigned char textSeedClickHint[] = { TEXT_SEED_CLICK_HINT };
    static unsigned char textSeedTypingHint[] = { TEXT_SEED_TYPING_HINT };

    print_hud_lut_string_fade(2, 100, 35, textEnterSeed);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, MIN(sTextBaseAlpha, 160));
    print_generic_string(88, 130,
                         gSeedTypingActive ? textSeedTypingHint : textSeedClickHint);

    // Labels under the two sub-buttons (buttons bottom out at y~60; keep a
    // visible gap so the text doesn't ride up onto the button faces).
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, sTextBaseAlpha);
    net_print_ascii_centered(114, 40, "OPTIONS");
    net_print_ascii_centered(206, 40, "START");

    if (gSeedTypingActive) {
        // The digits being typed, in the HUD font.
        u8 typed[12];
        s32 i, n = 0;
        for (i = 0; gSeedTypedBuf[i] != '\0' && n < 9; i++) {
            if (gSeedTypedBuf[i] >= '0' && gSeedTypedBuf[i] <= '9') {
                typed[n++] = gSeedTypedBuf[i] - '0';
            }
        }
        typed[n] = 0xFF;
        if (n > 0) {
            print_hud_lut_string_fade(2, 105, 70, typed);
        }
        gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
        return;
    }
    if (gBingoSeedIsSet) {
        xSeedPos = 105;
    } else {
        xSeedPos = 125;
    }
    print_hud_lut_string_fade(2, xSeedPos, 70, gBingoSeedText);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}
#endif

/**
 * Prints main menu strings that shows on the yellow background menu screen.
 * Does not print the strings of text for EU, only the symbols.
 */
static void print_main_menu_strings(void) {
#ifdef TARGET_N64
    draw_seed_mode_menu();
#else
    draw_main_menu_pc();
#endif
}

static unsigned char textGameMode[] = { TEXT_GAME_MODE };
static unsigned char text1Bingo[] = { TEXT_TARGET_1 };
static unsigned char text2Bingos[] = { TEXT_TARGET_2 };
static unsigned char text3Bingos[] = { TEXT_TARGET_3 };
static unsigned char textBlackout[] = { TEXT_TARGET_BLACKOUT };
static unsigned char textLockout[] = { TEXT_TARGET_LOCKOUT };

static unsigned char textUnlockGame[] = { TEXT_UNLOCK_GAME };
static unsigned char textNonstop[] = { TEXT_NONSTOP };
static unsigned char textTimeout[] = { TEXT_TIMEOUT };
static unsigned char textTimeout5[] = { TEXT_TIMEOUT_5 };
static unsigned char textTimeout15[] = { TEXT_TIMEOUT_15 };
static unsigned char textTimeout30[] = { TEXT_TIMEOUT_30 };
static unsigned char textTimeout45[] = { TEXT_TIMEOUT_45 };
static unsigned char textTimeout60[] = { TEXT_TIMEOUT_60 };
static unsigned char textEmpty[] = { 0xFF };

#ifndef TARGET_N64
static unsigned char textClaims[] = { TEXT_CLAIMS };
static unsigned char textLocations[] = { TEXT_LOCATIONS };
static unsigned char textClaimVisOpen[] = { TEXT_CLAIMVIS_OPEN };
static unsigned char textClaimVisProgress[] = { TEXT_CLAIMVIS_PROGRESS };
static unsigned char textClaimVisBingos[] = { TEXT_CLAIMVIS_BINGOS };
static unsigned char textClaimVisHidden[] = { TEXT_CLAIMVIS_HIDDEN };
#endif

static unsigned char textDPad[] = { TEXT_DPAD };
static unsigned char textPressA[] = { TEXT_PRESS_A };

static unsigned char textBingo64[] = { TEXT_BINGO64 };
static unsigned char textCreatedBy[] = { TEXT_CREATED_BY };
static unsigned char textContributionsFrom[] = { TEXT_CONTRIBUTIONS };
static unsigned char textSpecialThanks[] = { TEXT_SPECIAL_THANKS };
static unsigned char textSpecialThanks1[] = { TEXT_SPECIAL_THANKS_1 };
static unsigned char textSpecialThanks2[] = { TEXT_SPECIAL_THANKS_2 };
static unsigned char textSpecialThanks3[] = { TEXT_SPECIAL_THANKS_3 };
static unsigned char textSpecialThanks4[] = { TEXT_SPECIAL_THANKS_4 };
static unsigned char textSpecialThanks5[] = { TEXT_SPECIAL_THANKS_5 };
static unsigned char textSpecialThanks6[] = { TEXT_SPECIAL_THANKS_6 };
static unsigned char textSpecialThanks7[] = { TEXT_SPECIAL_THANKS_7 };
static unsigned char textSpecialThanks8[] = { TEXT_SPECIAL_THANKS_8 };
static unsigned char textSpecialThanks9[] = { TEXT_SPECIAL_THANKS_9 };
static unsigned char textSpecialThanks10[] = { TEXT_SPECIAL_THANKS_10 };
static unsigned char textSpecialThanks11[] = { TEXT_SPECIAL_THANKS_11 };
static unsigned char textSpecialThanks12[] = { TEXT_SPECIAL_THANKS_12 };
static unsigned char textSpecialThanks13[] = { TEXT_SPECIAL_THANKS_13 };
static unsigned char textSpecialThanks14[] = { TEXT_SPECIAL_THANKS_14 };


#define LEFT_X     24

// The config rows' value column: every value's RIGHT edge sits here.
// (Hand-tuned per-string x offsets drifted — "Lockout" and "Visible"
// hung short of the column; measure the string instead.)
#define BINGO_CONFIG_VALUE_RIGHT_X 158

static s32 bingo_config_value_x(u8 *target) {
    // Returns offsetX (relative to LEFT_X), right-aligning the value.
    return BINGO_CONFIG_VALUE_RIGHT_X - LEFT_X - get_string_width(target);
}

static s32 bingo_config_target(s32 i, u8 **target) {
    // Returns offsetX
    if (sToggleCurrentOption && sBingoOptionSelection == i) {
        sToggleCurrentOption = 0;
        gbBingoMode = (gbBingoMode + 1) % BINGO_MODE_COUNT;
    }
    switch (gbBingoMode) {
        default:
        case BINGO_MODE_LINE_1:
            *target = text1Bingo;
            break;
        case BINGO_MODE_LINE_2:
            *target = text2Bingos;
            break;
        case BINGO_MODE_LINE_3:
            *target = text3Bingos;
            break;
        case BINGO_MODE_BLACKOUT:
            *target = textBlackout;
            break;
        case BINGO_MODE_LOCKOUT:
            *target = textLockout;
            break;
    }
    return bingo_config_value_x(*target);
}

// The Timeout row: cycle OFF -> 5 -> 15 -> 30 -> 45 -> 60 minutes.
static s32 bingo_config_timeout(s32 i, u8 **target) {
    static const s32 choices[] = { 0, 5, 15, 30, 45, 60 };
    s32 j, cur = 0;
    for (j = 0; j < 6; j++) {
        if (gbBingoTimeout == choices[j]) {
            cur = j;
        }
    }
    if (sToggleCurrentOption && sBingoOptionSelection == i) {
        sToggleCurrentOption = 0;
        cur = (cur + 1) % 6;
        gbBingoTimeout = choices[cur];
    }
    switch (gbBingoTimeout) {
        default: *target = textOff;       break;
        case 5:  *target = textTimeout5;  break;
        case 15: *target = textTimeout15; break;
        case 30: *target = textTimeout30; break;
        case 45: *target = textTimeout45; break;
        case 60: *target = textTimeout60; break;
    }
    return bingo_config_value_x(*target);
}

#ifndef TARGET_N64
// The Claims row's value text and its right-ish x offset, tier-aware.
static s32 bingo_config_claimvis(s32 i, u8 **target) {
    if (sToggleCurrentOption && sBingoOptionSelection == i) {
        sToggleCurrentOption = 0;
        // Cycle, skipping tiers the current mode can't represent (in
        // lockout this sticks at Open, which IS the rule).
        do {
            gNetClaimVis = (gNetClaimVis + 1) % NET_CLAIMVIS_COUNT;
        } while (net_claimvis_coerce(gNetClaimVis, (s32) gbBingoMode)
                 != gNetClaimVis);
    }
    // The mode row may have invalidated the tier since it was set.
    gNetClaimVis = net_claimvis_coerce(gNetClaimVis, (s32) gbBingoMode);
    switch (gNetClaimVis) {
        default:
        case NET_CLAIMVIS_OPEN:     *target = textClaimVisOpen;     break;
        case NET_CLAIMVIS_PROGRESS: *target = textClaimVisProgress; break;
        case NET_CLAIMVIS_BINGOS:   *target = textClaimVisBingos;   break;
        case NET_CLAIMVIS_HIDDEN:   *target = textClaimVisHidden;   break;
    }
    return bingo_config_value_x(*target);
}
#endif

static void print_bingo_configs(void) {
    s32 i;
    s32 offsetX;
    u8 *label;
    u8 *target;

    s32 cfgs = BINGO_CONFIGS_IN_LEFT_COL;
    for (i = 0; i < cfgs; i++) {
        s32 y, shadowAlpha, textAlpha;
        label = textEmpty;
        target = textEmpty;
        offsetX = 0;
        // Every row runs (even scrolled out of view): the value helpers
        // also apply a pending toggle.
        if (i == 0) {
            label = textGameMode;
            offsetX = bingo_config_target(i, &target);
        } else if (i == 1) {
            label = textUnlockGame;
            if (sToggleCurrentOption && sBingoOptionSelection == i) {
                sToggleCurrentOption = 0;
                gBingoFullGameUnlocked ^= 1;
            }
            if (!gBingoFullGameUnlocked) {
                target = textOff;
            } else {
                target = textOn;
            }
            offsetX = bingo_config_value_x(target);
        } else if (i == 2) {
            label = textNonstop;
            if (sToggleCurrentOption && sBingoOptionSelection == i) {
                sToggleCurrentOption = 0;
                gBingoNonstop ^= 1;
            }
            target = gBingoNonstop ? textOn : textOff;
            offsetX = bingo_config_value_x(target);
        } else if (i == 3) {
            label = textTimeout;
            offsetX = bingo_config_timeout(i, &target);
#ifndef TARGET_N64
        } else if (i == 4) {
            label = textClaims;
            offsetX = bingo_config_claimvis(i, &target);
        } else if (i == 5) {
            label = textLocations;
            if (sToggleCurrentOption && sBingoOptionSelection == i) {
                sToggleCurrentOption = 0;
                gNetShowWhereabouts ^= 1;
            }
            target = gNetShowWhereabouts ? textOn : textOff;
            offsetX = bingo_config_value_x(target);
#endif
        }

        y = options_screen_y(options_settings_doc(i));
        shadowAlpha = options_edge_alpha(y, MIN(sTextBaseAlpha, 170));
        textAlpha = options_edge_alpha(y, MIN(sTextBaseAlpha, 200));
        if (textAlpha <= 0) {
            continue;
        }
        gDPSetEnvColor(gDisplayListHead++, 120, 120, 90, shadowAlpha);
        print_generic_string(LEFT_X + 2, y, label);
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 140, textAlpha);
        print_generic_string(LEFT_X + 1, y, label);
        gDPSetEnvColor(gDisplayListHead++, 120, 120, 90, shadowAlpha);
        print_generic_string(LEFT_X + offsetX + 1, y, target);
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 140, textAlpha);
        print_generic_string(LEFT_X + offsetX, y, target);
    }
}

// The settings section: the focused row's box, then the rows.
static void print_options_settings(void) {
    s32 i;
    if (sOptionsFocus == OPTIONS_FOCUS_SETTINGS) {
        s32 y = options_screen_y(options_settings_doc(sBingoOptionSelection));
        gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
        gDPSetPrimColor(gDisplayListHead++, 0, 0, 38, 38, 38,
                        options_edge_alpha(y, MIN(sTextBaseAlpha, 150)));
        grid_fill_rect(OPT_SETTINGS_X0, y, OPT_SETTINGS_X1, y + 16);
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    print_bingo_configs();
    // The key hints fill the rows' empty right column, right-aligned,
    // scrolling with the rows.
    for (i = 0; i < 2; i++) {
        u8 *hint = i == 0 ? textDPad : textPressA;
        s32 y = options_screen_y(options_settings_doc(0) + 14 * i);
        s32 alpha = options_edge_alpha(y, MIN(sTextBaseAlpha, 200) * 7 / 10);
        if (alpha > 0) {
            gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, alpha);
            print_generic_string(GRID_RIGHT_X - get_string_width(hint), y, hint);
        }
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

// Yellow-with-shadow text, the config rows' look (edge-faded: document
// text only).
static void print_grid_config_ascii(s32 x, s32 y, const char *str) {
    gDPSetEnvColor(gDisplayListHead++, 120, 120, 90,
                   options_edge_alpha(y, MIN(sTextBaseAlpha, 170)));
    grid_print_ascii(x + 1, y, str);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 140,
                   options_edge_alpha(y, MIN(sTextBaseAlpha, 200)));
    grid_print_ascii(x, y, str);
}

// The selection: a translucent box plus a 1-unit pulsing yellow frame on
// its edge. The icon box is 19 tall, not 20, so the frame's top line stays
// one unit clear of the band's leader rule (label_y + 4).
static void print_grid_highlight(void) {
    s32 x0, x1, y0, y1, pulse;
    if (sGridBand < 0) {
        // (The control row's box: drawn after the row's backing, by
        // print_grid_control_row, so the pinned row overlays the bands.)
        // A control: the band-label box, around the control's text.
        x0 = grid_control_x(sGridCol) - 2;
        x1 = grid_control_end_x(sGridCol) + 3;
        y0 = grid_ctrl_y() + 2;
        y1 = y0 + 14;
    } else if (sGridCol < 0) {
        x0 = GRID_LEFT_X;
        x1 = GRID_LABEL_X + grid_band_label_width(sGridBand) + 3;
        y0 = grid_label_y(sGridBand) + 2;
        y1 = y0 + 14;
    } else {
        x0 = grid_icon_x(sGridCol) - 2;
        x1 = x0 + 20;
        y0 = grid_icon_y(sGridBand, sGridCol) - 2;
        y1 = y0 + 19;
    }
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 38, 38, 38,
                    options_edge_alpha(y0, MIN(sTextBaseAlpha, 150)));
    grid_fill_rect(x0, y0, x1, y1);

    pulse = 150 + (s32) (100.0f * (0.5f + 0.5f * sins(gGlobalTimer * 0x600)));
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 140,
                    options_edge_alpha(y0, MIN(sTextBaseAlpha, pulse)));
    grid_fill_rect(x0, y1 - 1, x1, y1);              // top
    grid_fill_rect(x0, y0, x1, y0 + 1);              // bottom
    grid_fill_rect(x0, y0 + 1, x0 + 1, y1 - 1);      // left
    grid_fill_rect(x1 - 1, y0 + 1, x1, y1 - 1);      // right
}

// Leader rules between each band's label and its count, so the headers
// read as a table.
static void print_grid_band_rules(void) {
    s32 b;
    char text[16];
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
    for (b = 0; b < GRID_BAND_COUNT; b++) {
        s32 ly = grid_label_y(b), x0, x1;
        s32 alpha = options_edge_alpha(ly, MIN(sTextBaseAlpha, 60));
        if (alpha <= 0) {
            continue;
        }
        grid_count_text(text, grid_band_enabled(b), sGridBands[b].count);
        x0 = GRID_LABEL_X + grid_band_label_width(b) + 6;
        x1 = GRID_RIGHT_X - grid_ascii_width(text) - 6;
        if (x1 > x0) {
            gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, alpha);
            grid_fill_rect(x0, ly + 4, x1, ly + 5);
        }
    }
}

// The footer names what A would act on (the selected objective, shown
// with its icon and state, or the band's all-on/all-off action), with the
// pool's enabled total right-aligned. Pinned (drawn outside the content
// window's scissor), and only while the focus is in the grid.
static void print_grid_footer(void) {
    s32 whiteTextAlpha = MIN(sTextBaseAlpha, 200);
    char text[40];
    char *p;
    s32 i, total = 0;
    s32 gatedNote = 0;
    if (sGridBand >= 0 && sGridCol >= 0) {
        // The selected objective again, beside its name.
        gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
        print_bingo_icon_alpha(GRID_LABEL_X, GRID_FOOTER_Y,
                               get_objective_info(sGridBands[sGridBand].types[sGridCol])->icon,
                               gOptionSelectIconOpacity);
        gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha);
    if (sGridBand < 0) {
        grid_print_ascii(GRID_LABEL_X, GRID_FOOTER_Y,
                         sGridCol == 0 ? "Loadout: objectives, mode, unlock"
                                       : "Turn every objective on or off");
    } else if (sGridCol < 0 && grid_band_gated(sGridBand)) {
        p = grid_append(text, sGridBands[sGridBand].label);
        grid_append(p, ": needs Unlock OFF");
        grid_print_ascii(GRID_LABEL_X, GRID_FOOTER_Y, text);
    } else if (sGridCol < 0) {
        p = grid_append(text, sGridBands[sGridBand].label);
        grid_append(p, grid_band_enabled(sGridBand) == sGridBands[sGridBand].count
                           ? ": A turns all OFF"
                           : ": A turns all ON");
        grid_print_ascii(GRID_LABEL_X, GRID_FOOTER_Y, text);
    } else {
        u8 type = sGridBands[sGridBand].types[sGridCol];
        u8 *name = get_objective_info(type)->optionText;
        s32 x = GRID_FOOTER_NAME_X;
        print_generic_string(x, GRID_FOOTER_Y, name);
        x += get_string_width(name) + 8;
        if (!grid_objective_on(type)) {
            gDPSetEnvColor(gDisplayListHead++, 255, 80, 80, whiteTextAlpha);
            print_generic_string(x, GRID_FOOTER_Y, textOff);
            gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha);
        } else {
            print_generic_string(x, GRID_FOOTER_Y, textOn);
        }
        if (grid_objective_gated(type)) {
            // OFF because unlock is ON: say why where the total usually sits.
            gatedNote = 1;
        }
    }
    if (gatedNote) {
        grid_text_color(255, 255, 140, whiteTextAlpha);
        grid_print_ascii(GRID_FOOTER_TOTAL_RIGHT_X - grid_ascii_width(GRID_GATED_NOTE), GRID_FOOTER_Y,
                         GRID_GATED_NOTE);
    } else {
        for (i = 0; i < GRID_BAND_COUNT; i++) {
            total += grid_band_enabled(i);
        }
        grid_count_text(text, total, BINGO_OBJECTIVE_TOTAL_AMOUNT);
        grid_text_color(255, 255, 255, whiteTextAlpha);
        grid_print_ascii(GRID_FOOTER_TOTAL_RIGHT_X - grid_ascii_width(text), GRID_FOOTER_Y, text);
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

// The control row above the bands, band-header style: yellow labels,
// white value.
// Drawn after the bands so the pinned row overlays them: its backing
// (while pinned), its selection box, then the text.
static void print_grid_control_row(void) {
    s32 y = grid_ctrl_y();
    s32 x = GRID_LABEL_X + grid_ascii_width("PRESET") + GRID_CTRL_GAP;
    if (options_edge_alpha(y, 255) <= 0) {
        return;
    }
    if (grid_ctrl_pinned()) {
        gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
        gDPSetPrimColor(gDisplayListHead++, 0, 0, 38, 38, 38,
                        options_edge_alpha(y, MIN(sTextBaseAlpha, 200)));
        grid_fill_rect(GRID_CTRL_BACK_X0, y - 3, GRID_CTRL_BACK_X1, y + 17);
    }
    if (sOptionsFocus == OPTIONS_FOCUS_GRID && sGridBand < 0) {
        print_grid_highlight();
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    print_grid_config_ascii(GRID_LABEL_X, y, "PRESET");
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255,
                   options_edge_alpha(y, MIN(sTextBaseAlpha, 200)));
    grid_print_ascii(x, y, grid_preset_name());
    print_grid_config_ascii(GRID_CTRL_TOGGLE_X, y, "TOGGLE ALL");
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

static void print_objective_grid(void) {
    s32 b, c;
    char text[16];

    grid_clamp_cursor();

    print_grid_band_rules();
    if (sOptionsFocus == OPTIONS_FOCUS_GRID && sGridBand >= 0) {
        print_grid_highlight();
    }

    // Icons: OFF ones stay visible but dimmed, so the whole pool reads at
    // a glance.
    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    for (b = 0; b < GRID_BAND_COUNT; b++) {
        for (c = 0; c < sGridBands[b].count; c++) {
            u8 type = sGridBands[b].types[c];
            s32 iy = grid_icon_y(b, c);
            // Progression objectives read as OFF while unlock is ON.
            s32 dim = !grid_objective_on(type);
            s32 alpha = options_edge_alpha(iy, dim ? MIN(gOptionSelectIconOpacity, 70)
                                                   : gOptionSelectIconOpacity);
            if (alpha > 0) {
                print_bingo_icon_alpha(grid_icon_x(c), iy,
                                       get_objective_info(type)->icon, alpha);
            }
        }
    }
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);

    // Band headers: label left, "n/m" right-aligned into one column,
    // coloured all on (white) / some (yellow) / none (red).
    for (b = 0; b < GRID_BAND_COUNT; b++) {
        s32 ly = grid_label_y(b), n;
        s32 alpha = options_edge_alpha(ly, MIN(sTextBaseAlpha, 200));
        if (alpha <= 0) {
            continue;
        }
        print_grid_config_ascii(GRID_LABEL_X, ly, sGridBands[b].label);
        n = grid_band_enabled(b);
        grid_count_text(text, n, sGridBands[b].count);
        if (n == sGridBands[b].count) {
            grid_text_color(255, 255, 255, alpha);
        } else if (n > 0) {
            grid_text_color(255, 255, 140, alpha);
        } else {
            grid_text_color(255, 80, 80, alpha);
        }
        grid_print_ascii(GRID_RIGHT_X - grid_ascii_width(text), ly, text);
    }

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);

    if (grid_controls_visible()) {
        print_grid_control_row();
    }
}

// The credits, at the document's end.
static void print_options_credits(void) {
    static unsigned char *const left[OPT_CREDITS_LINES] = {
        textBingo64, textCreatedBy, textContributionsFrom, textSpecialThanks,
        textSpecialThanks1, textSpecialThanks2, textSpecialThanks3, textSpecialThanks4,
        textSpecialThanks5, textSpecialThanks6, textSpecialThanks7,
    };
    // Only the special-thanks lines have a right column.
    static unsigned char *const right[OPT_CREDITS_LINES] = {
        NULL, NULL, NULL, NULL,
        textSpecialThanks8, textSpecialThanks9, textSpecialThanks10, textSpecialThanks11,
        textSpecialThanks12, textSpecialThanks13, textSpecialThanks14,
    };
    // Each line's x, relative to the credits' centre-left column (90).
    static const s8 dx[OPT_CREDITS_LINES] = { 38, 0, -20, 26, -40, -40, -40, -40, -40, -40, -40 };
    s32 i, y, alpha, x, doc = options_credits_doc();
    s32 base = MIN(sTextBaseAlpha, 200) * 7 / 10;

    // Underlines below the two headers (BINGO 64, Special thanks).
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
    for (i = 0; i <= 3; i += 3) {
        y = options_screen_y(doc + OPT_CREDITS_PITCH * i);
        alpha = options_edge_alpha(y, base);
        if (alpha > 0) {
            x = 90 + dx[i] + 6;
            gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, alpha);
            grid_fill_rect(x, y, x + (i == 0 ? 39 : 69), y + 1);
        }
    }

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    for (i = 0; i < OPT_CREDITS_LINES; i++) {
        y = options_screen_y(doc + OPT_CREDITS_PITCH * i);
        alpha = options_edge_alpha(y, base);
        if (alpha <= 0) {
            continue;
        }
        x = 90 + dx[i] + 6;
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, alpha);
        print_generic_string(x, y, left[i]);
        if (right[i] != NULL) {
            print_generic_string(x + 80, y, right[i]);
        }
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

// A scroll arrow: five stacked rows 1..9 units wide (the slash's
// hand-drawn fill-rect technique), bottom row at y0.
static void print_options_arrow(s32 y0, s32 up, s32 canScroll) {
    s32 k, alpha = 60;
    if (canScroll) {
        alpha = 160 + (s32) (60.0f * sins(gGlobalTimer * 0x800));
    }
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, MIN(sTextBaseAlpha, alpha));
    for (k = 0; k < 5; k++) {
        s32 half = up ? 4 - k : k;
        grid_fill_rect(OPT_ARROW_X - half, y0 + k, OPT_ARROW_X + half + 1, y0 + k + 1);
    }
}

static void print_bingo_options(void) {
    if (gOptionSelectIconOpacity <= 10) {
        return;
    }
#ifndef TARGET_N64
    // Room settings are the host's, and freeze once the race starts.
    if (bingo_options_locked()) {
        gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
        gDPSetEnvColor(gDisplayListHead++, 255, 120, 120, MIN(sTextBaseAlpha, 220));
        net_print_ascii(24, 212,
                        network_room_locked() ? "LOCKED. THE RACE HAS STARTED"
                                              : "THE HOST CONTROLS THESE SETTINGS");
        gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
    }
#endif
    // The scrolling document, clipped to the content window.
    gDPPipeSync(gDisplayListHead++);
    gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, SCREEN_HEIGHT - OPT_CONTENT_TOP,
                  SCREEN_WIDTH, SCREEN_HEIGHT - OPT_CONTENT_BOTTOM);
    print_options_settings();
    print_objective_grid();
    print_options_credits();
    gDPPipeSync(gDisplayListHead++);
    gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Pinned footer strip: the grid's footer, the scroll arrows, BACK.
    if (sOptionsFocus == OPTIONS_FOCUS_GRID) {
        print_grid_footer();
    }
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
    print_options_arrow(OPT_ARROW_UP_Y, 1, sOptionsScrollTarget > 0);
    print_options_arrow(OPT_ARROW_DOWN_Y, 0, sOptionsScrollTarget < options_max_scroll());
#ifndef TARGET_N64
    // Clickable BACK tag; mouse users had no visible way out (the screen
    // only exited on B/ESC).
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 140,
                   options_back_tag_hovered() ? sTextBaseAlpha
                                              : MIN(sTextBaseAlpha, 170));
    net_print_ascii(options_back_x(), GRID_FOOTER_Y, "BACK");
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
#endif
    if (sToggleCurrentOption) {
        sToggleCurrentOption = 0;
    }
}
#undef LEFT_X


#undef PRINT_COURSE_NAME_CN
#undef PRINT_COURSE_SCORES_CN
#undef PRINT_COURSE_NAME_AND_SCORES

#ifdef WIDESCREEN
/**
 * Copy of the X values of the vertices so they can be intialized independently.
 */
const short sGeneralButtonVtxPosXGroup1[] = {
  -163,  -122,  -163,  -143,  -133,  -133,  -133,   133,  -133,   133,   133,  -133,  -143,   143,   133,   143,
};

const short sGeneralButtonVtxPosXGroup2[] = {
   143,   133,   133,   133,  -143,   143,  -143,   133,   143,  -133,  -143,  -133,  -143,   163,  -143,  -163,
};

const short sGeneralButtonVtxPosXGroup3[] = {
   163,   143,  -143,   143,   163,  -163,  -143,  -163,   163,   122,  -122,  -122,  -122,  -163,
};

const short sGeneralButtonVtxPosXGroup4[] = {
  -122,  -122,   122,  -163,   163,  -122,  -122,   122,   163,  -163,   122,   163,   122,   163,   122,
};

const short sSaveButtonBackVtxPosX[] = {
   163,  -163,   163,  -163,
};

extern Vtx vertex_menu_main_button_dynamic_group1[];
extern Vtx vertex_menu_main_button_dynamic_group2[];
extern Vtx vertex_menu_main_button_dynamic_group3[];
extern Vtx vertex_menu_main_button_dynamic_group4[];
extern Vtx vertex_menu_save_button_back[];

void file_select_fit_screen(void) {
    // color buttons vtx
    Vtx *vtxColorButton1 = segmented_to_virtual(vertex_menu_main_button_dynamic_group1);
    Vtx *vtxColorButton2 = segmented_to_virtual(vertex_menu_main_button_dynamic_group2);
    Vtx *vtxColorButton3 = segmented_to_virtual(vertex_menu_main_button_dynamic_group3);
    Vtx *vtxColorButton4 = segmented_to_virtual(vertex_menu_main_button_dynamic_group4);
    // save button vtx
    Vtx *vtxSaveBackButton = segmented_to_virtual(vertex_menu_save_button_back);
    
    // NOTE: This may look like a hack but this is better than scaling because it doesn't
    // look weird when it gets more wide, lighting also gets weird with scaling.
    // This workaround moves the tris to adapt the screen without looking weird.

    vtxColorButton1[0].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup1[0]);
    vtxColorButton1[1].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup1[1]);
    vtxColorButton1[2].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup1[2]);
    vtxColorButton1[3].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup1[3]);
    vtxColorButton1[4].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup1[4]);
    vtxColorButton1[5].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup1[5]);
    vtxColorButton1[6].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup1[6]);
    vtxColorButton1[7].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup1[7]);
    vtxColorButton1[8].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup1[8]);
    vtxColorButton1[9].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup1[9]);
    vtxColorButton1[10].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup1[10]);
    vtxColorButton1[11].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup1[11]);
    vtxColorButton1[12].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup1[12]);
    vtxColorButton1[13].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup1[13]);
    vtxColorButton1[14].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup1[14]);
    vtxColorButton1[15].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup1[15]);
    
    vtxColorButton2[0].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup2[0]);
    vtxColorButton2[1].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup2[1]);
    vtxColorButton2[2].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup2[2]);
    vtxColorButton2[3].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup2[3]);
    vtxColorButton2[4].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup2[4]);
    vtxColorButton2[5].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup2[5]);
    vtxColorButton2[6].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup2[6]);
    vtxColorButton2[7].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup2[7]);
    vtxColorButton2[8].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup2[8]);
    vtxColorButton2[9].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup2[9]);
    vtxColorButton2[10].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup2[10]);
    vtxColorButton2[11].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup2[11]);
    vtxColorButton2[12].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup2[12]);
    vtxColorButton2[13].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup2[13]);
    vtxColorButton2[14].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup2[14]);
    vtxColorButton2[15].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup2[15]);
    
    vtxColorButton3[0].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup3[0]);
    vtxColorButton3[1].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup3[1]);
    vtxColorButton3[2].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup3[2]);
    vtxColorButton3[3].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup3[3]);
    vtxColorButton3[4].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup3[4]);
    vtxColorButton3[5].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup3[5]);
    vtxColorButton3[6].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup3[6]);
    vtxColorButton3[7].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup3[7]);
    vtxColorButton3[8].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup3[8]);
    vtxColorButton3[9].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup3[9]);
    vtxColorButton3[10].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup3[10]);
    vtxColorButton3[11].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup3[11]);
    vtxColorButton3[12].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup3[12]);
    vtxColorButton3[13].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup3[13]);
    
    vtxColorButton4[0].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup4[0]);
    vtxColorButton4[1].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup4[1]);
    vtxColorButton4[2].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup4[2]);
    vtxColorButton4[3].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup4[3]);
    vtxColorButton4[4].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup4[4]);
    vtxColorButton4[5].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup4[5]);
    vtxColorButton4[6].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup4[6]);
    vtxColorButton4[7].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup4[7]);
    vtxColorButton4[8].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup4[8]);
    vtxColorButton4[9].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup4[9]);
    vtxColorButton4[10].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup4[10]);
    vtxColorButton4[11].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup4[11]);
    vtxColorButton4[12].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup4[12]);
    vtxColorButton4[13].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup4[13]);
    vtxColorButton4[14].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sGeneralButtonVtxPosXGroup4[14]);

    vtxSaveBackButton[0].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sSaveButtonBackVtxPosX[0]);
    vtxSaveBackButton[1].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sSaveButtonBackVtxPosX[1]);
    vtxSaveBackButton[2].n.ob[0] = GFX_DIMENSIONS_FROM_RIGHT_EDGE(320 - sSaveButtonBackVtxPosX[2]);
    vtxSaveBackButton[3].n.ob[0] = GFX_DIMENSIONS_FROM_LEFT_EDGE(sGeneralButtonVtxPosXGroup4[3]);
}
#endif

/**
 * Prints file select strings depending on the menu selected.
 * Also checks if all saves exists and defines text and main menu timers.
 */
static void print_file_select_strings(void) {
    create_dl_ortho_matrix();
    switch (sSelectedButtonID) {
        case MENU_BUTTON_NONE:
#ifdef VERSION_EU
            // Ultimately calls print_main_menu_strings, but prints main language strings first.
            print_main_lang_strings();
#else
            print_main_menu_strings();
#endif
            break;
        case MENU_BUTTON_SEED_OPTION:
            print_bingo_options();
            break;
#ifndef TARGET_N64
        case MENU_BUTTON_PLAY_FILE_A:
            draw_1p_setup();
            break;
        case MENU_BUTTON_ONLINE:
            online_lobby_draw(sTextBaseAlpha, sCursorPos[0] + 160.0f,
                              sCursorPos[1] + 120.0f);
            break;
        case MENU_BUTTON_SETTINGS:
            if (optmenu_open) {
                optmenu_y_offset = 20;  // centered box on this page's backdrop
                optmenu_draw();
                optmenu_y_offset = 0;
                optmenu_draw_prompt();  // "R: Return"
                if (gMenuTextAlpha < 250) {
                    gMenuTextAlpha += 25;  // prompt alpha, normally pause-driven
                }
            }
            break;
#endif
    }
    // Timers for menu alpha text and the main menu itself
    if (sTextBaseAlpha < 250) {
        sTextBaseAlpha += 10;
    }
    if (sMainMenuTimer < 1000) {
        sMainMenuTimer++;
    }
    gOptionSelectIconOpacity = sTextBaseAlpha;
    
#ifdef WIDESCREEN
    file_select_fit_screen();
#endif

#ifdef KEY_COMBO_SKIP_INTRO_CUTSCENE
    // Adds key combo to skip intro cutscene, useful on Non-PC targets
    if ((gPlayer1Controller->buttonDown == (L_TRIG | R_TRIG)) && sCurrentMenuLevel == MENU_LAYER_MAIN) {
        if (!(gGlobalGameSkips & GAME_SKIP_INTRO_SCENE)) {
            play_sound(SOUND_MENU_STAR_SOUND, gGlobalSoundSource);
            gGlobalGameSkips |= GAME_SKIP_INTRO_SCENE;
        }
    }
#endif
}

/**
 * Geo function that prints file select strings and the cursor.
 */
Gfx *geo_file_select_strings_and_menu_cursor(s32 callContext, UNUSED struct GraphNode *node, UNUSED Mat4 mtx) {
    if (callContext == GEO_CONTEXT_RENDER) {
#ifdef TARGET_N3DS
        gDPForceFlush(gDisplayListHead++);
        gDPSet2d(gDisplayListHead++, 1);
        gDPSetIod(gDisplayListHead++, iodFileSelect);
#endif
        print_file_select_strings();
#ifndef TARGET_N64
        // The settings screen has no clickable elements; a parked hand
        // cursor on top of the options menu just reads as a glitch.
        if (sSelectedButtonID != MENU_BUTTON_SETTINGS)
#endif
        {
            print_menu_cursor();
        }
#ifdef TARGET_N3DS
        gDPForceFlush(gDisplayListHead++);
        gDPSet2d(gDisplayListHead++, 0);
#endif
    }
    return NULL;
}

/**
 * Initiates file select values after Mario Screen.
 * Relocates cursor position of the last save if the game goes back to the Mario Screen
 * either completing a course choosing "SAVE & QUIT" or having a game over.
 */
s32 lvl_init_menu_values_and_cursor_pos(UNUSED s32 arg, UNUSED s32 unused) {
#ifdef VERSION_EU
    s8 fileIndex;
#endif
    sSelectedButtonID = MENU_BUTTON_NONE;
    sCurrentMenuLevel = MENU_LAYER_MAIN;
    sTextBaseAlpha = 0;
#ifndef TARGET_N64
    s1PExitRequest = 0;
    sLobbyExitRequest = 0;
    sSettingsExitRequest = 0;
    sSettingsMenuEntered = 0;
    gSeedTypingActive = 0;
#endif
#ifndef TARGET_N64
    // Spawn the hand dead center, just below and between the two doors.
    sCursorPos[0] = 0.0f;
    sCursorPos[1] = -35.0f;
#else
    sCursorPos[0] = 94.0f;
    sCursorPos[1] = 20.0f;
#endif
    sClickPos[0] = -10000;
    sClickPos[1] = -10000;
    sCursorClickingTimer = 0;
    sSelectedFileNum = 0;
    sFadeOutText = FALSE;
    sTextFadeAlpha = 0;
    sMainMenuTimer = 0;
    sSoundMode = save_file_get_sound_mode();
#ifdef VERSION_EU
    sLanguageMode = eu_get_language();

    for (fileIndex = 0; fileIndex <= 3; fileIndex++) {
        if (save_file_exists(fileIndex) == TRUE) {
            sOpenLangSettings = FALSE;
            break;
        } else {
            sOpenLangSettings = TRUE;
        }
    }
#endif

    return 0;
}

u32 get_seed(void) {
#ifndef TARGET_N64
    // Online play: every player in the room uses the server's shared seed.
    {
        u32 netSeed;
        if (network_has_seed(&netSeed)) {
            return netSeed;
        }
    }
#endif
#ifndef TARGET_N64
    // Debug: a fixed board for screenshot runs (test/pc).
    if (getenv("BINGO64_SEED") != NULL) {
        return (u32) strtoul(getenv("BINGO64_SEED"), NULL, 10);
    }
#endif
    if (!gBingoSeedIsSet) {
        init_genrand(gGlobalTimer);
        return random_u32() % 999999999;
    }
    return bingo_seed_proposal();
}

/**
 * Updates file select menu button objects so they can be interacted.
 * When a save file is selected, it returns fileNum value
 * defined in load_main_menu_save_file.
 */
s32 lvl_update_obj_and_load_file_selected(UNUSED s32 arg, UNUSED s32 unused) {
    area_update_objects();
    if (sSelectedFileNum && !gBingoInitialized) {
        gBingoSeed = get_seed();
        setup_bingo_objectives(gBingoSeed);
        if (gBingoFullGameUnlocked) {
            unlock_full_game();
        } else {
            // A fresh file, whatever this slot held before (an earlier
            // race, or the unlock-ON stamp): the progression objectives
            // and vanilla door/act gating assume one.
            save_file_reset_for_race(sSelectedFileNum - 1);
        }
    }
    return sSelectedFileNum;
}

#undef FILE_SELECT_PRINT_STRING
#undef FILE_SELECT_TEXT_DL_BEGIN
#undef FILE_SELECT_TEXT_DL_END
