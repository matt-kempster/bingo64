#include <ultra64.h>
#include <PR/os_cont.h>
#include <PR/os_libc.h>

#include "types.h"
#include "game.h"
#include "sm64.h"
#include "print.h"
#include "hud.h"
#include "area.h"
#include "save_file.h"
#include "bingo.h"
#include "bingo_ui.h"
#include "bingo_descriptions.h"
#include "ingame_menu.h"
#include "menu/file_select.h"
#include "engine/behavior_script.h"
#include "level_update.h"
#include "strcpy.h"

// Cursor positions. -1 indicates that the cursor has not spawned yet.
// 0-indexed with bottom-left as (0, 0)
static s32 sBingoCursorX = -1;
static s32 sBingoCursorY = -1;

// Cursor default positions.
#define BINGO_CURSOR_X_DEFAULT 2
#define BINGO_CURSOR_Y_DEFAULT 2

// Cursor timing
#define BINGO_CURSOR_TIMEOUT_FRAMES 6
static s32 sBingoCursorTimer = 0; // if nonzero, don't move cursor

int bingostrlen(char *str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

void draw_bingo_win_screen() {
    char timestamp[10];
    char msg[40];

    print_text(30, 40, "YOU ARE A SUPER PLAYER");
    getTimeFmt(timestamp, gbGlobalBingoTimer);
    sprintf(msg, "YOUR TIME WAS %s", timestamp);
    print_text(40, 60, msg);
}

void draw_bingo_hud_timer() {
    s32 i, j;
    s32 count = 0;
    struct BingoObjective *objective;
    const char empty_string[50] = { '\0' };
    char buffer[50];
    char buffer2[50];
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            objective = &gBingoObjectives[5 * i + j];
            if (objective->type == BINGO_OBJECTIVE_STAR_TIMED
                && objective->data.starTimerObjective.course == gCurrCourseNum
                && objective->state != BINGO_STATE_FAILED_IN_THIS_COURSE
                && objective->state != BINGO_STATE_COMPLETE) {
                getTimeFmtTiny(buffer, objective->data.starTimerObjective.maxTime
                                           - objective->data.starTimerObjective.timer);
                sprintf(buffer2, "%c%d: %s", 0xFA, objective->data.starTimerObjective.starIndex + 1,
                        buffer);
                print_text_not_tiny(242, 190 - (18 * (count + 1)), buffer2);
                count++;
                strcpy(buffer, empty_string);
                strcpy(buffer2, empty_string);
            }
        }
    }
    if (count != 0) {
        print_text_not_tiny(230, 190, "Time remaining:\xFF");
    }
}

void bingo_print_description(char *str) {
    int last_space = 0;
    int last_space_line_chars = 0;
    int line_chars = 0;
    s32 iter = 0;
    s32 total_lines = 0;
    u8 finalDesc[150] = { 0x11, 0x28, 0x2F, 0x2F, 0xFF };

    while (str[iter] != '\0') {
        line_chars++;
        // Chop the line if it's getting too long
        if (str[iter] == ' ') {
            if (line_chars >= 25) {
                line_chars = iter - last_space_line_chars;
                finalDesc[last_space] = 0xFE;
                // update number of lines
                total_lines++;
            }
            last_space = iter;
            last_space_line_chars = iter;
        }
        finalDesc[iter] = str[iter]; // tiny_text_convert_ascii(str[iter]);
        iter++;
    }
    // sue me
    if (line_chars >= 25) {
        finalDesc[last_space] = 0xFE;
    }
    finalDesc[iter] = 0xFF;

    print_text_not_tiny(180, 100 + total_lines * 10, finalDesc);
}

void draw_bingo_screen() {
    struct BingoObjective *objective;
    int i, j, length;
    int spacing = 35;
    char icon[2];
    char desc_text[300];
    char seed_print[20];

    // Title.
    char *bingo[5] = { "B", "I", "N", "G", "O" };
    for (i = 0; i < 5; i++) {
        print_text_large(6 + spacing * i, HUD_TOP_Y + 5, bingo[i]);
    }

    // Seed.
    sprintf(seed_print, "SEED %09d", gBingoInitialSeed);
    print_text_tiny(240, 214, seed_print);

    // Lines.
    for (i = 0; i < 4; i++) {
        print_vertical_line(25 + spacing * i, HUD_TOP_Y - 36);
    }
    for (i = 0; i < 4; i++) {
        print_horizontal_line(37 + spacing * i);
    }

    // Objectives.
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            objective = &gBingoObjectives[5 * i + j];
            // Icons.
            switch (objective->state) {
                case BINGO_STATE_COMPLETE:
                    strcpy(icon, ICON_COMPLETE);
                    break;
                case BINGO_STATE_FAILED_IN_THIS_COURSE:
                    strcpy(icon, ICON_FAILED);
                    break;
                default:
                    strcpy(icon, objective->icon);
                    break;
            }
            print_text(11 + spacing * j, 28 + spacing * i, icon);
            // Subtitles.
            length = strlen_tiny(objective->title);
            print_text_tiny(11 + spacing * j + 8 - (length) / 2 + 1, 75 + spacing * (4 - i),
                            objective->title);
        }
    }

    // Update cursor on button press.
    if (gPlayer1Controller->buttonDown & JPAD_BUTTONS && !sBingoCursorTimer) {
        if (sBingoCursorX == -1) {
            sBingoCursorX = BINGO_CURSOR_X_DEFAULT;
            sBingoCursorY = BINGO_CURSOR_Y_DEFAULT;
        } else {
            if (gPlayer1Controller->buttonDown & U_JPAD) {
                sBingoCursorY = (sBingoCursorY + 1) % 5;
            } else if (gPlayer1Controller->buttonDown & D_JPAD) {
                sBingoCursorY = (sBingoCursorY - 1 + 5) % 5;
            } else if (gPlayer1Controller->buttonDown & L_JPAD) {
                sBingoCursorX = (sBingoCursorX - 1 + 5) % 5;
            } else if (gPlayer1Controller->buttonDown & R_JPAD) {
                sBingoCursorX = (sBingoCursorX + 1) % 5;
            }
        }
        sBingoCursorTimer = BINGO_CURSOR_TIMEOUT_FRAMES;
    } else if (sBingoCursorTimer > 0) {
        sBingoCursorTimer--;
    }

    // Print cursor and details.
    if (sBingoCursorX != -1) {
        print_hand(14 + spacing * sBingoCursorX, 18 + spacing * sBingoCursorY);

        objective = &gBingoObjectives[5 * sBingoCursorY + sBingoCursorX];
        describe_objective(objective, desc_text);
        bingo_print_description(desc_text);
    }

    return;
}
