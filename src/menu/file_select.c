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
#include "game/area.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/print.h"
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
#include "game/strcpy.h"
#include "engine/rand.h"
#include "game/save_file.h"

#include "eu_translation.h"
#ifdef VERSION_EU
#undef LANGUAGE_FUNCTION
#define LANGUAGE_FUNCTION sLanguageMode
#endif

/**
 * @file file_select.c
 * This file implements how the file select and it's menus render and function.
 * That includes button IDs rendered as object models, strings, hand cursor,
 * special menu messages and phases, button states and button clicked checks.
 */

#ifdef VERSION_US
// The current sound mode is automatically centered on US due to
// the large length difference between options.
// sSoundTextY unused (EU supports its existence).
static s16 sSoundTextX;
static s16 sSoundTextY;
#endif

//! @Bug (UB Array Access) For EU, more buttons were added than the array was extended.
//! This causes no currently known issues on console (as the other variables are not changed
//! while this is used) but can cause issues with other compilers.
#ifdef VERSION_EU
    #ifdef AVOID_UB
        #define NUM_BUTTONS 36
    #else
        #define NUM_BUTTONS 34
    #endif
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
static unsigned char textNew[] = { TEXT_NEW };
static unsigned char starIcon[] = { GLYPH_STAR, GLYPH_SPACE };
static unsigned char xIcon[] = { GLYPH_MULTIPLY, GLYPH_SPACE };
#endif

static unsigned char textSelectFile[] = { TEXT_SELECT_FILE };
static unsigned char textSeeds[] = { TEXT_SEEDS };
static unsigned char textReset[] = { TEXT_RESET };
static unsigned char textRandom[] = { TEXT_RANDOM };
static unsigned char textOption[] = { TEXT_OPTION };
static unsigned char textStart[] = { TEXT_START };
static unsigned char textBackspace[] = { TEXT_BACKSPACE };
static unsigned char textOff[] = { TEXT_OFF };
static unsigned char textOn[] = { TEXT_ON };


s32 gBingoSeedIsSet = 0;
// We can support seeds up to 4,294,967,295, but since this is a weird number,
// we cap it at 999,999,999, which is 9 digits long. "RANDOM" is 6 characters
// long, so:
u8 gBingoSeedRandomText[] = { TEXT_RANDOM, 0xFF, 0xFF, 0xFF };
u8 gBingoSeedText[] = { TEXT_RANDOM, 0xFF, 0xFF, 0xFF };

s32 sBingoOptionSelection = 0;
#define BINGO_ENTRIES_PER_COL 11
#define BINGO_CONFIGS_IN_LEFT_COL 3 // not more than 10, hopefully
#define BINGO_OPTIONS_IN_LEFT_COL_FIRST_PAGE (BINGO_ENTRIES_PER_COL - BINGO_CONFIGS_IN_LEFT_COL)
#define BINGO_INSTRUCTIONS_IN_RIGHT_COL 3
#define BINGO_OPTIONS_IN_RIGHT_COL (BINGO_ENTRIES_PER_COL - BINGO_INSTRUCTIONS_IN_RIGHT_COL)
#define BINGO_OPTIONS_IN_FIRST_PAGE (BINGO_OPTIONS_IN_LEFT_COL_FIRST_PAGE + BINGO_OPTIONS_IN_RIGHT_COL)
#define BINGO_OPTIONS_IN_SECONDARY_PAGES (BINGO_ENTRIES_PER_COL + BINGO_OPTIONS_IN_RIGHT_COL)
#define BINGO_OPTIONS_TOTAL_ENTRIES_PER_PAGE (BINGO_ENTRIES_PER_COL * 2)
s32 sBingoOptionSelectTimer = 0;
#define BINGO_OPTION_TIMER_FRAMES 3
s32 sToggleCurrentOption = 0;
s32 sBingoOptionCurrentPage = 0;
#define BINGO_MAX_PAGE_INDEX 3


/**
 * Yellow Background Menu Initial Action
 * Rotates the background at 180 grades and it's scale.
 * Although the scale is properly applied in the loop function.
 */
void beh_yellow_background_menu_init(void) {
    gCurrentObject->oFaceAngleYaw = 0x8000;
    gCurrentObject->oMenuButtonScale = 9.0f;
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
        button->oMenuButtonTimer = 0;
    }
}

/**
 * Shrink back to submenu, used to return back while inside a score save menu.
 */
static void bhv_menu_button_shrinking_to_submenu(struct Object *button) {
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
        play_sound(SOUND_MENU_CAMERA_ZOOM_OUT, gDefaultSoundArgs);
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
#undef ACTION_TIMER
#undef MAIN_RETURN_TIMER

#ifdef VERSION_EU
    #define SOUND_BUTTON_Y 388
#else
    #define SOUND_BUTTON_Y 0
#endif

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
                    play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gDefaultSoundArgs);
                    gOptionSelectIconOpacity = 0;
                    sMainMenuButtons[buttonId]->oMenuButtonState = MENU_BUTTON_STATE_GROWING;
                    sSelectedButtonID = buttonId;
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

/**
 * Loads a save file selected after it goes into a full screen state
 * retuning sSelectedFileNum to a save value defined in fileNum.
 */
void load_main_menu_save_file(struct Object *fileButton, s32 fileNum) {
    if (fileButton->oMenuButtonState == MENU_BUTTON_STATE_FULLSCREEN) {
        sSelectedFileNum = fileNum;
    }
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
        play_sound(SOUND_MENU_CAMERA_ZOOM_OUT, gDefaultSoundArgs);
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
    enum MenuButtonTypes buttonID;
    u8 buttonNum;
    s16 buttonX;
    s16 buttonY;

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

    sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A] = spawn_object_rel_with_rot(
        gCurrentObject, MODEL_MAIN_MENU_GREEN_SCORE_BUTTON, bhvMenuButton, 6800, 1000, 0, 0, 0, 0
    );
    sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A]->oMenuButtonScale = 1.0f;

    sMainMenuButtons[MENU_BUTTON_SEED_OPTION] = spawn_object_rel_with_rot(
        gCurrentObject, MODEL_MAIN_MENU_BLUE_COPY_BUTTON, bhvMenuButton, 6800, -3800, 0, 0, 0, 0
    );
    sMainMenuButtons[MENU_BUTTON_SEED_OPTION]->oMenuButtonScale = 1.0f;


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
            play_sound(SAVE_FILE_SOUND, gDefaultSoundArgs);
            break;
    }
}
#undef SAVE_FILE_SOUND

/**
 * Menu Buttons Menu Manager Loop Action
 * Calls a menu function depending of the button chosen.
 * sSelectedButtonID is MENU_BUTTON_NONE when the file select
 * is loaded, and that checks what buttonID is clicked in the main menu.
 */
void bhv_menu_button_manager_loop(void) {
    switch (sSelectedButtonID) {
        case MENU_BUTTON_NONE:
            check_main_menu_clicked_buttons();
            seed_menu_check_clicked_buttons();
            break;
        case MENU_BUTTON_PLAY_FILE_A:
            load_main_menu_save_file(sMainMenuButtons[MENU_BUTTON_PLAY_FILE_A], 1);
            break;
        case MENU_BUTTON_SEED_OPTION:
            exit_score_file_to_score_menu(sMainMenuButtons[MENU_BUTTON_SEED_OPTION], MENU_BUTTON_NONE);
            break;
    }

    sClickPos[0] = -10000;
    sClickPos[1] = -10000;
}

/**
 * Cursor function that handles button inputs.
 * If the cursor is clicked, sClickPos uses the same value as sCursorPos.
 */
static void handle_cursor_button_input(void) {
    if (sSelectedButtonID == MENU_BUTTON_SEED_OPTION) {
        if (gPlayer3Controller->buttonPressed & (B_BUTTON | START_BUTTON)) {
            sTextBaseAlpha = 0;
            gOptionSelectIconOpacity = 0;

            sClickPos[0] = sCursorPos[0];
            sClickPos[1] = sCursorPos[1];
            sCursorClickingTimer = 1;
        } else {
            if (sBingoOptionSelectTimer > 0) {
                sBingoOptionSelectTimer--;
            } else if (gPlayer3Controller->buttonPressed & (D_JPAD | D_CBUTTONS)) {
                if (sBingoOptionSelection < BINGO_ENTRIES_PER_COL) {
                    sBingoOptionSelection = (sBingoOptionSelection + 1) % BINGO_ENTRIES_PER_COL;
                } else {
                    sBingoOptionSelection =
                        (sBingoOptionSelection - BINGO_ENTRIES_PER_COL + 1)
                        % BINGO_ENTRIES_PER_COL
                        + BINGO_ENTRIES_PER_COL;
                }
                sBingoOptionSelectTimer = BINGO_OPTION_TIMER_FRAMES;
            } else if (gPlayer3Controller->buttonPressed & (U_JPAD | U_CBUTTONS)) {
                if (sBingoOptionSelection < BINGO_ENTRIES_PER_COL) {
                    sBingoOptionSelection =
                        (sBingoOptionSelection + BINGO_ENTRIES_PER_COL - 1) % BINGO_ENTRIES_PER_COL;
                } else {
                    sBingoOptionSelection =
                        (
                            (sBingoOptionSelection - BINGO_ENTRIES_PER_COL)
                            + (BINGO_ENTRIES_PER_COL - 1)
                        ) % BINGO_ENTRIES_PER_COL
                        + BINGO_ENTRIES_PER_COL;
                }
                sBingoOptionSelectTimer = BINGO_OPTION_TIMER_FRAMES;
            } else if (gPlayer3Controller->buttonPressed & (L_JPAD | R_JPAD | L_CBUTTONS | R_CBUTTONS)) {
                sBingoOptionSelection += BINGO_ENTRIES_PER_COL;
                sBingoOptionSelection %= (BINGO_OPTIONS_TOTAL_ENTRIES_PER_PAGE);
                sBingoOptionSelectTimer = BINGO_OPTION_TIMER_FRAMES;
            } else if (gPlayer3Controller->buttonPressed & L_TRIG) {
                if (sBingoOptionCurrentPage == 0) {
                    sBingoOptionCurrentPage = BINGO_MAX_PAGE_INDEX;
                } else {
                    sBingoOptionCurrentPage--;
                }
            } else if (gPlayer3Controller->buttonPressed & R_TRIG) {
                if (sBingoOptionCurrentPage == BINGO_MAX_PAGE_INDEX) {
                    sBingoOptionCurrentPage = 0;
                } else {
                    sBingoOptionCurrentPage++;
                }
            }
            if (gPlayer3Controller->buttonPressed & A_BUTTON) {
                sToggleCurrentOption = 1;
            }
        }
    } else { // If cursor is clicked
        if (gPlayer3Controller->buttonPressed
#ifdef VERSION_EU
            & (A_BUTTON | B_BUTTON | START_BUTTON | Z_TRIG)) {
#else
            & (A_BUTTON | B_BUTTON | START_BUTTON)) {
#endif
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
    s16 rawStickX = gPlayer3Controller->rawStickX;
    s16 rawStickY = gPlayer3Controller->rawStickY;

    // Handle deadzone
    if (rawStickY > -2 && rawStickY < 2) {
        rawStickY = 0;
    }
    if (rawStickX > -2 && rawStickX < 2) {
        rawStickX = 0;
    }

    // Move cursor
    sCursorPos[0] += rawStickX / 8;
    sCursorPos[1] += rawStickY / 8;

    // Stop cursor from going offscreen
    if (sCursorPos[0] > 132.0f) {
        sCursorPos[0] = 132.0f;
    }
    if (sCursorPos[0] < -132.0f) {
        sCursorPos[0] = -132.0f;
    }

    if (sCursorPos[1] > 90.0f) {
        sCursorPos[1] = 90.0f;
    }
    if (sCursorPos[1] < -90.0f) {
        sCursorPos[1] = -90.0f;
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
    if (sCursorClickingTimer == 0)
        // Idle
        gSPDisplayList(gDisplayListHead++, dl_menu_idle_hand);
    if (sCursorClickingTimer != 0)
        // Grabbing
        gSPDisplayList(gDisplayListHead++, dl_menu_grabbing_hand);
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
void print_hud_lut_string_fade(s8 hudLUT, s16 x, s16 y, const unsigned char *text) {
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, sTextBaseAlpha - sTextFadeAlpha);
    print_hud_lut_string(hudLUT, x, y, text);
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_end);
}

/**
 * Prints a generic white string with text fade properties.
 */
void print_generic_string_fade(s16 x, s16 y, const unsigned char *text) {
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, sTextBaseAlpha - sTextFadeAlpha);
    print_generic_string(x, y, text);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

static void draw_seed_mode_menu(void) {
    s32 xSeedPos;
    unsigned char textEnterSeed[] = { TEXT_ENTER_SEED };
    // Display "ENTER SEED" text
    print_hud_lut_string_fade(2, 100, 35, textEnterSeed);

    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_end);  // TODO: needed?
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);

    // Display return, reset
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, sTextBaseAlpha);
    print_generic_string(47, 34, textReset);
    print_generic_string(241, 34, textOption);
    print_generic_string(245, 104, textStart);
    print_generic_string(35, 104, textBackspace);

    // Display seed
    if (gBingoSeedIsSet) {
        xSeedPos = 105;
    } else {
        xSeedPos = 125;
    }
    print_hud_lut_string_fade(2, xSeedPos, 100 + (30 * -1), gBingoSeedText);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

/**
 * Prints main menu strings that shows on the yellow background menu screen.
 * Does not print the strings of text for EU, only the symbols.
 */
static void print_main_menu_strings(void) {
    draw_seed_mode_menu();
}

static unsigned char textGameMode[] = { TEXT_GAME_MODE };
static unsigned char text1Bingo[] = { TEXT_TARGET_1 };
static unsigned char text2Bingos[] = { TEXT_TARGET_2 };
static unsigned char text3Bingos[] = { TEXT_TARGET_3 };
static unsigned char textBlackout[] = { TEXT_TARGET_BLACKOUT };

static unsigned char textUnlockGame[] = { TEXT_UNLOCK_GAME };
static unsigned char textToggleAll[] = { TEXT_TOGGLE_ALL };
static unsigned char textEmpty[] = { 0xFF };

static unsigned char textDPad[] = { TEXT_DPAD };
static unsigned char textPressA[] = { TEXT_PRESS_A };
static unsigned char textPressRL_1[] = { TEXT_PRESS_RL_1 };
static unsigned char textPressRL_2[] = { TEXT_PRESS_RL_2 };
static unsigned char textPressRL_3[] = { TEXT_PRESS_RL_3 };

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
#define RIGHT_X    160
#define TOP_Y      12
#define ROW_HEIGHT 17

static void print_bingo_selection_highlight(void) {
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 38, 38, 38, MIN(sTextBaseAlpha, 150));
    if (sBingoOptionSelection < BINGO_ENTRIES_PER_COL) {
        gDPFillRectangle(
            gDisplayListHead++,
            LEFT_X,
            TOP_Y - 2 + ROW_HEIGHT * (sBingoOptionSelection + 1),
            RIGHT_X,
            TOP_Y - 2 + ROW_HEIGHT * (sBingoOptionSelection + 2) - 1
        );
    } else {
        gDPFillRectangle(
            gDisplayListHead++,
            RIGHT_X,
            TOP_Y - 2 + ROW_HEIGHT * ((sBingoOptionSelection % BINGO_ENTRIES_PER_COL) + 1),
            RIGHT_X + (RIGHT_X - LEFT_X),
            TOP_Y - 2 + ROW_HEIGHT * ((sBingoOptionSelection % BINGO_ENTRIES_PER_COL) + 2) - 1
        );
    }
}

void print_option_nav_instructions(s32 pageNo) {
    s32 i;
    unsigned char *option;
    s32 optionLeftX;
    s32 offsetY;
    for (i = BINGO_OPTIONS_TOTAL_ENTRIES_PER_PAGE - 3; i < BINGO_OPTIONS_TOTAL_ENTRIES_PER_PAGE; i++) {
        offsetY = ROW_HEIGHT * (BINGO_ENTRIES_PER_COL - (i % BINGO_ENTRIES_PER_COL)) - 2;
        option = NULL;
        switch (i) {
            case BINGO_OPTIONS_TOTAL_ENTRIES_PER_PAGE - 3:
                option = textDPad;
                optionLeftX = RIGHT_X + 8;
                break;
            case BINGO_OPTIONS_TOTAL_ENTRIES_PER_PAGE - 2:
                option = textPressA;
                optionLeftX = RIGHT_X + 46;
                break;
            case BINGO_OPTIONS_TOTAL_ENTRIES_PER_PAGE - 1:
                switch (pageNo) {
                    case 0:
                        option = textPressRL_1;
                        break;
                    case 1:
                        option = textPressRL_2;
                        break;
                    case 2:
                        option = textPressRL_3;
                        break;
                }
                optionLeftX = RIGHT_X + 15;
                break;
        }
        if (option != NULL) {
            print_generic_string(optionLeftX, TOP_Y + offsetY, option);
        }
    }
}
#include "game/bingo_objective_info.h"
static void print_objective(enum BingoObjectiveType type, s32 pageNo) {
    enum BingoObjectiveIcon obj_icon;
    unsigned char *option;
    s32 optionLeftX;
    s32 onOrOffLeftX;
    s32 offsetY;
    s32 leftColAmount;
    s32 page_index;
    s32 objectives_on_prior_pages;
    s32 whiteTextAlpha = MIN(sTextBaseAlpha, 200);

    if (pageNo == 0) {
        objectives_on_prior_pages = 0;
    } else {
        objectives_on_prior_pages = (
            BINGO_OPTIONS_IN_FIRST_PAGE
            + BINGO_OPTIONS_IN_SECONDARY_PAGES * (pageNo - 1)
        );
    }

    if (
        (
            (pageNo == 0 && type == (sBingoOptionSelection - BINGO_CONFIGS_IN_LEFT_COL))
            || (
                pageNo != 0
                && sBingoOptionSelection == (
                    (type - objectives_on_prior_pages) % BINGO_OPTIONS_TOTAL_ENTRIES_PER_PAGE
                )
            )
        )
        && sToggleCurrentOption
    ) {
        sToggleCurrentOption = 0;
        gBingoObjectivesDisabled[type] ^= 1;
    }

    option = get_objective_info(type)->optionText;
    obj_icon = get_objective_info(type)->icon;
    page_index = type - objectives_on_prior_pages;

    if (pageNo == 0) {
        leftColAmount = BINGO_OPTIONS_IN_LEFT_COL_FIRST_PAGE;
    } else {
        leftColAmount = BINGO_ENTRIES_PER_COL;
    }
    if (page_index < leftColAmount) {
        optionLeftX = LEFT_X + 1;
        onOrOffLeftX = RIGHT_X - 19;
        offsetY = ROW_HEIGHT * (
            leftColAmount - page_index
        ) - 2;
    } else {
        optionLeftX = RIGHT_X;
        onOrOffLeftX = RIGHT_X + (RIGHT_X - LEFT_X) - 19;
        offsetY = ROW_HEIGHT * (
            BINGO_OPTIONS_TOTAL_ENTRIES_PER_PAGE
            - page_index
            - (pageNo == 0 ? BINGO_CONFIGS_IN_LEFT_COL : 0)
        ) - 2;
    }
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha);
    print_generic_string(optionLeftX + 19, TOP_Y + offsetY, option);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    print_bingo_icon(optionLeftX, TOP_Y + offsetY, obj_icon);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha);

    if (gBingoObjectivesDisabled[type]) {
        gDPSetEnvColor(gDisplayListHead++, 255, 80, 80, sTextBaseAlpha);
        print_generic_string(onOrOffLeftX, TOP_Y + offsetY, textOff);
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha);
    } else {
        print_generic_string(onOrOffLeftX, TOP_Y + offsetY, textOn);
    }
}

static s32 bingo_config_target(s32 i, u8 **target) {
    // Returns offsetX
    if (sToggleCurrentOption && sBingoOptionSelection == i) {
        sToggleCurrentOption = 0;
        switch (gbBingoTarget) {
            case 1:
                gbBingoTarget = 2;
                break;
            case 2:
                gbBingoTarget = 3;
                break;
            case 3:
                gbBingoTarget = 12;
                break;
            case 12:
                gbBingoTarget = 1;
                break;
        }
    }
    switch (gbBingoTarget) {
        case 1:
            *target = text1Bingo;
            return 94;
        case 2:
            *target = text2Bingos;
            return 89;
        case 3:
            *target = text3Bingos;
            return 89;
        case 12:
            *target = textBlackout;
            return 92;
    }
}

static void print_bingo_configs() {
    s32 i;
    s32 offsetX;
    u8 *label;
    u8 *target;

    for (i = 0; i < BINGO_CONFIGS_IN_LEFT_COL; i++) {
        switch (i) {
            case 0:
                label = textGameMode;
                offsetX = bingo_config_target(i, &target);
                break;
            case 2:
                label = textToggleAll;
                if (sToggleCurrentOption && sBingoOptionSelection == i) {
                    sToggleCurrentOption = 0;
                    for (i = 0; i < BINGO_OBJECTIVE_TOTAL_AMOUNT; i++) {
                        gBingoObjectivesDisabled[i] ^= 1;
                    }
                }
                target = textEmpty;
                offsetX = 0;
                break;
            case 1:
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
                offsetX = RIGHT_X - 19 - LEFT_X;
                break;
        }

        gDPSetEnvColor(gDisplayListHead++, 120, 120, 90, MIN(sTextBaseAlpha, 170));
        print_generic_string(
            LEFT_X + 2,
            TOP_Y + ROW_HEIGHT * (BINGO_ENTRIES_PER_COL - i) - 2,
            label
        );
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 140, MIN(sTextBaseAlpha, 200));
        print_generic_string(
            LEFT_X + 1,
            TOP_Y + ROW_HEIGHT * (BINGO_ENTRIES_PER_COL - i) - 2,
            label
        );
        gDPSetEnvColor(gDisplayListHead++, 120, 120, 90, MIN(sTextBaseAlpha, 170));
        print_generic_string(
            LEFT_X + offsetX + 1,
            TOP_Y + ROW_HEIGHT * (BINGO_ENTRIES_PER_COL - i) - 2,
            target
        );
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 140, MIN(sTextBaseAlpha, 200));
        print_generic_string(
            LEFT_X + offsetX,
            TOP_Y + ROW_HEIGHT * (BINGO_ENTRIES_PER_COL - i) - 2,
            target
        );
    }
}

static void print_bingo_page_0() {
    s32 i;
    s32 whiteTextAlpha = MIN(sTextBaseAlpha, 200);

    print_bingo_selection_highlight();

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha);

    print_bingo_configs();

    for (i = 0; i < MIN(BINGO_OBJECTIVE_TOTAL_AMOUNT, BINGO_OPTIONS_IN_FIRST_PAGE); i++) {
        print_objective(i, 0);
    }
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha * 0.7);
    print_option_nav_instructions(0);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}


static void print_bingo_middle_page(s32 pageNo) {
    s32 i;
    s32 whiteTextAlpha = MIN(sTextBaseAlpha, 200);

    print_bingo_selection_highlight();
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha);

    // Print configs here (TODO)

    // Print objectives

    for (
        i = BINGO_OPTIONS_IN_FIRST_PAGE + (pageNo - 1) * BINGO_OPTIONS_IN_SECONDARY_PAGES;
        i < MIN(BINGO_OBJECTIVE_TOTAL_AMOUNT, BINGO_OPTIONS_IN_FIRST_PAGE + pageNo * BINGO_OPTIONS_IN_SECONDARY_PAGES);
        i++
    ) {
        print_objective(i, pageNo);
    }
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha * 0.7);
    print_option_nav_instructions(pageNo);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

static void print_line(s32 startX, s32 length, s32 y, s32 alpha) {
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, alpha);
    gDPFillRectangle(gDisplayListHead++, startX, y - 1, startX + length, y);
}

static void print_bingo_page_2(void) {
    s32 i;
    unsigned char *creditString;
    unsigned char *creditString2;
    s32 optionLeftX = 90;
    s32 offsetY;
    s32 whiteTextAlpha = MIN(sTextBaseAlpha, 200);
    s32 creditsLeftX;

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha * 0.7);
    for (i = 0; i < 11; i++) {
        offsetY = ROW_HEIGHT * (BINGO_ENTRIES_PER_COL - i) - 2;
        creditString = NULL;
        switch (i) {
            case 0:
                creditString = textBingo64;
                creditsLeftX = optionLeftX + 38;
                gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
                print_line(
                    creditsLeftX + 6,
                    39,
                    TOP_Y - 2 + ROW_HEIGHT * (i + 2) - 1,
                    whiteTextAlpha * 0.7
                );
                gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
                gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha * 0.7);

                break;
            case 1:
                creditString = textCreatedBy;
                creditsLeftX = optionLeftX;
                break;
            case 2:
                creditString = textContributionsFrom;
                creditsLeftX = optionLeftX - 20;
                break;
            case 3:
                creditString = textSpecialThanks;
                creditsLeftX = optionLeftX + 26;
                gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
                print_line(
                    creditsLeftX + 6,
                    69,
                    TOP_Y - 2 + ROW_HEIGHT * (i + 2) - 1,
                    whiteTextAlpha * 0.7
                );
                gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
                gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, whiteTextAlpha * 0.7);
                break;
            case 4:
                creditString = textSpecialThanks1;
                creditString2 = textSpecialThanks8;
                creditsLeftX = optionLeftX - 40;
                break;
            case 5:
                creditString = textSpecialThanks2;
                creditString2 = textSpecialThanks9;
                creditsLeftX = optionLeftX - 40;
                break;
            case 6:
                creditString = textSpecialThanks3;
                creditString2 = textSpecialThanks10;
                creditsLeftX = optionLeftX - 40;
                break;
            case 7:
                creditString = textSpecialThanks4;
                creditString2 = textSpecialThanks11;
                creditsLeftX = optionLeftX - 40;
                break;
            case 8:
                creditString = textSpecialThanks5;
                creditString2 = textSpecialThanks12;
                creditsLeftX = optionLeftX - 40;
                break;
            case 9:
                creditString = textSpecialThanks6;
                creditString2 = textSpecialThanks13;
                creditsLeftX = optionLeftX - 40;
                break;
            case 10:
                creditString = textSpecialThanks7;
                creditString2 = textSpecialThanks14;
                creditsLeftX = optionLeftX - 40;
                break;
        }
        if (creditString != NULL) {
            print_generic_string(creditsLeftX + 6, TOP_Y + offsetY, creditString);
        }
        if (creditString2 != NULL) {
            print_generic_string(creditsLeftX + 6 + 80, TOP_Y + offsetY, creditString2);
        }
    }

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}


static void print_bingo_options(void) {
    if (gOptionSelectIconOpacity <= 10) {
        return;
    }
    switch (sBingoOptionCurrentPage) {
        case 0:
            print_bingo_page_0();
            break;
        case 1:
            print_bingo_middle_page(1);
            break;
        case 2:
            print_bingo_middle_page(2);
            break;
        case 3:
            print_bingo_page_2();
            break;
    }
    if (sToggleCurrentOption) {
        sToggleCurrentOption = 0;
    }
}
#undef LEFT_X
#undef RIGHT_X
#undef TOP_Y
#undef ROW_HEIGHT


/**
 * Prints file select strings depending on the menu selected.
 * Also checks if all saves exists and defines text and main menu timers.
 */
static void print_file_select_strings(void) {
    UNUSED s32 unused1;
    UNUSED s32 unused2;

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
    }
    // Timers for menu alpha text and the main menu itself
    if (sTextBaseAlpha < 250) {
        sTextBaseAlpha += 10;
    }
    if (sMainMenuTimer < 1000) {
        sMainMenuTimer += 1;
    }
    gOptionSelectIconOpacity = sTextBaseAlpha;
}

/**
 * Geo function that prints file select strings and the cursor.
 */
Gfx *geo_file_select_strings_and_menu_cursor(s32 callContext, UNUSED struct GraphNode *node, UNUSED Mat4 mtx) {
    if (callContext == GEO_CONTEXT_RENDER) {
        print_file_select_strings();
        print_menu_cursor();
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
    s8 fileNum;
#endif
    sSelectedButtonID = MENU_BUTTON_NONE;
    sCurrentMenuLevel = MENU_LAYER_MAIN;
    sTextBaseAlpha = 0;
    sCursorPos[0] = 94.0f;
    sCursorPos[1] = 20.0f;
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

    for (fileNum = 0; fileNum < 4; fileNum++) {
        if (save_file_exists(fileNum) == TRUE) {
            sOpenLangSettings = FALSE;
            break;
        } else {
            sOpenLangSettings = TRUE;
        }
    }
#endif
    //! no return value
#ifdef AVOID_UB
    return 0;
#endif
}

u32 get_seed(void) {
    if (!gBingoSeedIsSet) {
        init_genrand(gGlobalTimer);
        return random_u32() % 999999999;
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
        }
    }
    return sSelectedFileNum;
}
