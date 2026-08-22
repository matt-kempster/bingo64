#include <stdio.h>
#include <stdlib.h>

#include <ultra64.h>
#include <PR/os_cont.h>
#include <PR/os_libc.h>

#include "types.h"
#include "game_init.h"
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
#include "segment2.h"
#include "ingame_menu.h"
#include "bingo_objective_info.h"
#include "segment2.h"
#include "extras/draw_util.h"
#ifndef TARGET_N64
#include "pc/network/network.h"
#endif

s8 gBingoAllowBoardToShow;
s8 gForceDrawBingoScreen = 0;

#define BINGO_MIN_X 21
#define BINGO_MAX_X 230
#define BINGO_MIN_Y 10
#define BINGO_MAX_Y 209

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

#ifndef TARGET_N64
// "matt" -> "MATT" in a static buffer, for the all-caps HUD font.
static const char *hud_upper(const char *name) {
    static char buf[NET_NAME_LEN];
    s32 i;
    for (i = 0; name[i] != '\0' && i < NET_NAME_LEN - 1; i++) {
        buf[i] = (name[i] >= 'a' && name[i] <= 'z') ? name[i] - 0x20 : name[i];
    }
    buf[i] = '\0';
    return buf;
}

static const char *net_name_of_id(s32 id) {
    s32 i;
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        if (gNetPlayers[i].active && gNetPlayers[i].id == id) {
            return hud_upper(gNetPlayers[i].name);
        }
    }
    return "?";
}

// Cells each room member owns (their bit is set in gBingoCellClaimers).
static s32 net_cell_count_of_id(s32 id) {
    s32 i, count = 0;
    if (id < 0 || id >= 32) {
        return 0;
    }
    for (i = 0; i < 25; i++) {
        if (gBingoCellClaimers[i] & ((u32) 1 << id)) {
            count++;
        }
    }
    return count;
}
#endif

// --- In-game notices --------------------------------------------------
// Subtitle-style toast feed for online events (someone claimed a square,
// joined/left/reconnected, the host changed, the race was ended). Pushed
// by the PC network client and bingo_net; drawn bottom-center as dialog-
// font text on a dimmed strip, newest at the bottom, ~4s each with a
// fade-out. A toast optionally opens with the actor's name in their hat
// color and closes with the claimed square's real board icon. On N64
// nothing pushes, so it all compiles to a no-op.

#define BINGO_NOTICE_MAX 3
#define BINGO_NOTICE_LEN 48
#define BINGO_NOTICE_NAME_LEN 20
#define BINGO_NOTICE_LIFE 120   // frames a notice stays up (4 seconds)
#define BINGO_NOTICE_FADE 30    // fade-out tail within that life

struct BingoNotice {
    char name[BINGO_NOTICE_NAME_LEN]; // "" = no colored prefix
    char text[BINGO_NOTICE_LEN];
    u8 rgb[3];                        // name color (hat color)
    s16 icon;                         // BingoObjectiveIcon, -1 = none
    u32 frame;                        // gGlobalTimer at push
};

static struct BingoNotice sNotices[BINGO_NOTICE_MAX];
static s32 sNoticeCount = 0;

static void bingo_notice_drop_oldest(void) {
    s32 i;
    for (i = 1; i < sNoticeCount; i++) {
        sNotices[i - 1] = sNotices[i];
    }
    sNoticeCount--;
}

void bingo_notice_rich(const char *name, const u8 rgb[3], const char *text,
                       s32 icon) {
    struct BingoNotice *n;
    s32 i;
    if (sNoticeCount == BINGO_NOTICE_MAX) {
        bingo_notice_drop_oldest();
    }
    n = &sNotices[sNoticeCount];
    for (i = 0; name[i] != '\0' && i < BINGO_NOTICE_NAME_LEN - 1; i++) {
        n->name[i] = name[i];
    }
    n->name[i] = '\0';
    for (i = 0; text[i] != '\0' && i < BINGO_NOTICE_LEN - 1; i++) {
        n->text[i] = text[i];
    }
    n->text[i] = '\0';
    n->rgb[0] = rgb[0];
    n->rgb[1] = rgb[1];
    n->rgb[2] = rgb[2];
    // Only icons the info table can texture (FAILED/SUCCESS are special-
    // cased by print_bingo_icon_alpha and always safe).
    if (icon > BINGO_ICON_SUCCESS
        && get_objective_info_from_icon(icon) == NULL) {
        icon = -1;
    }
    n->icon = (s16) icon;
    n->frame = gGlobalTimer;
    sNoticeCount++;
}

void bingo_notice(const char *text) {
    static const u8 white[3] = { 255, 255, 255 };
    bingo_notice_rich("", white, text, -1);
}

// The latest toast's full line ("name text"), for the lobby status row.
const char *bingo_notice_latest(u32 *ageFrames) {
    static char composed[BINGO_NOTICE_NAME_LEN + BINGO_NOTICE_LEN];
    struct BingoNotice *n;
    if (sNoticeCount == 0) {
        return NULL;
    }
    n = &sNotices[sNoticeCount - 1];
    *ageFrames = gGlobalTimer - n->frame;
    sprintf(composed, "%s%s%s", n->name, n->name[0] != '\0' ? " " : "",
            n->text);
    return composed;
}

#ifndef TARGET_N64
// Dev-only: BINGO64_TOAST_DEMO=1 cycles sample toasts through the feed so
// the rendering can be screenshot-tested without a live room.
static void bingo_notice_demo_tick(void) {
    static s32 next = 0;
    if (getenv("BINGO64_TOAST_DEMO") == NULL || gGlobalTimer % 90 != 0) {
        return;
    }
    switch (next++ % 4) {
        case 0:
            bingo_notice_rich("Quate", gNetColorRGB[5], "got",
                              BINGO_ICON_STAR);
            break;
        case 1:
            bingo_notice_rich("leGlitch", gNetColorRGB[1], "got",
                              BINGO_ICON_COIN);
            break;
        case 2:
            bingo_notice_rich("Boop", gNetColorRGB[3], "reconnected", -1);
            break;
        case 3:
            bingo_notice("the host ended the race");
            break;
    }
}
#endif

void draw_bingo_notices(void) {
#ifndef TARGET_N64
    s32 i;
    bingo_notice_demo_tick();
    while (sNoticeCount > 0
           && gGlobalTimer - sNotices[0].frame > BINGO_NOTICE_LIFE) {
        bingo_notice_drop_oldest();
    }
    for (i = 0; i < sNoticeCount; i++) {
        // Oldest reads first: it sits highest, newest at the bottom.
        s32 row = sNoticeCount - 1 - i;
        s32 yText = 24 + 21 * row;
        struct BingoNotice *n = &sNotices[i];
        u32 age = gGlobalTimer - n->frame;
        s32 alpha = 255;
        s32 nameW, textW, iconW, total, x;
        if (age > BINGO_NOTICE_LIFE - BINGO_NOTICE_FADE) {
            alpha = 255 * (BINGO_NOTICE_LIFE - (s32) age) / BINGO_NOTICE_FADE;
            if (alpha <= 0) {
                continue;
            }
        }
        nameW = n->name[0] != '\0' ? get_string_width_ascii(n->name) + 4 : 0;
        textW = n->text[0] != '\0' ? get_string_width_ascii(n->text) : 0;
        iconW = n->icon >= 0 ? 20 : 0;
        total = nameW + textW + iconW;
        x = 20;  // bottom-left aligned; strips grow rightward per row

        // The dimmed strip (fillrect coords: origin top-left, y down).
        print_solid_color_quad(x - 6, SCREEN_HEIGHT - (yText + 13),
                               x + total + 6, SCREEN_HEIGHT - (yText - 6),
                               0, 0, 0, (u8) (140 * alpha / 255));

        // Text rides 1px high and icons 2px low of the naive positions so
        // both center optically in the 19px strip (screenshot-tuned).
        gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
        if (n->name[0] != '\0') {
            print_generic_string_ascii_detail(x, yText + 1, n->name,
                                              n->rgb[0], n->rgb[1], n->rgb[2],
                                              (u8) alpha, TRUE, 1);
        }
        if (n->text[0] != '\0') {
            print_generic_string_ascii_detail(x + nameW, yText + 1, n->text,
                                              255, 255, 255, (u8) alpha,
                                              TRUE, 1);
        }
        gSPDisplayList(gDisplayListHead++, dl_ia_text_end);

        if (n->icon >= 0) {
            gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
            print_bingo_icon_alpha(x + nameW + textW + 4, yText - 5, n->icon,
                                   (u8) alpha);
            gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
        }
    }
#endif
}

// Once first place is taken in an online line/blackout race, the players
// who did NOT win need to hear about it too — persistently, not as a 5s
// toast. Lockout is excluded: its verdict ends the race for everyone and
// draw_bingo_win_screen already shows it. Clears when the results clear
// (back to lobby / disconnect).
void draw_bingo_race_verdict(void) {
#ifndef TARGET_N64
    s32 i, winnerId = 0, myPlace;
    char buf[64];
    if (!network_active() || gbBingoMode == BINGO_MODE_LOCKOUT
        || !gBingoInitialized) {
        return;
    }
    for (i = 0; i < network_result_count(); i++) {
        if (network_result(i)->place == 1) {
            winnerId = network_result(i)->id;
        }
    }
    if (winnerId == 0 || winnerId == network_local_id()) {
        return;  // no verdict yet, or it's ours (the win overlay's job)
    }
    myPlace = network_local_place();
    if (myPlace > 0) {
        sprintf(buf, "%s WON - YOU PLACED %d", net_name_of_id(winnerId),
                myPlace);
    } else {
        sprintf(buf, "%s WON - RACE FOR PLACE %d", net_name_of_id(winnerId),
                network_result_count() + 1);
    }
    print_text_centered(160, 189, buf);
#endif
}

void draw_bingo_win_screen() {
    char timestamp[16];
    char msg[40];

#ifndef TARGET_N64
    if (network_active() && gbBingoMode == BINGO_MODE_LOCKOUT
        && network_race_winner_id() != 0) {
        // Lockout ends for the whole room at once: show the verdict.
        s32 winner = network_race_winner_id();
        if (winner == network_local_id()) {
            sprintf(msg, "YOU WIN %d SQUARES", net_cell_count_of_id(winner));
        } else {
            sprintf(msg, "%s WINS %d SQUARES", net_name_of_id(winner),
                    net_cell_count_of_id(winner));
        }
        print_text(30, 60, msg);
        if (gbBingoShowCongratsCounter == (gbBingoShowCongratsLimit - 1)) {
            print_text(60, 40, "PRESS L AGAIN TO");
            print_text(110, 20, "DISMISS");
        }
        return;
    }
    if (network_active() && network_local_place() > 0) {
        // A race finish: our official place and server-timed result.
        getTimeFmtPrecise(timestamp, gbGlobalBingoTimer);
        sprintf(msg, "FINISHED NUMBER %d IN %s", network_local_place(), timestamp);
        print_text(16, 60, msg);
        if (gbBingoShowCongratsCounter == (gbBingoShowCongratsLimit - 1)) {
            print_text(60, 40, "PRESS L AGAIN TO");
            print_text(110, 20, "DISMISS");
        } else {
            print_text(30, 40, "YOU ARE A SUPER PLAYER");
        }
        return;
    }
#endif

    getTimeFmtPrecise(timestamp, gbGlobalBingoTimer);
    sprintf(msg, "YOUR TIME IS %s", timestamp);
    // TODO: insert 0/0.5 spaces at front to center align:
    print_text(30, 60, msg);

    if (gbBingoShowCongratsCounter == (gbBingoShowCongratsLimit - 1)) {
        print_text(60, 40, "PRESS L AGAIN TO");
        print_text(110, 20, "DISMISS");
    } else {
        print_text(30, 40, "YOU ARE A SUPER PLAYER");
    }
}

void draw_bingo_hud_timer() {
    s32 i, j;
    s32 count = 0;
    struct BingoObjective *objective;
    const char empty_string[50] = { '\0' };
    char buffer[50];
    char buffer2[50];
    s32 yOffset = (
        // Koopa the Quick stars, where a timer might show
        (gCurrCourseNum == COURSE_BOB && gCurrActNum == 2)
        || (gCurrCourseNum == COURSE_THI && gCurrActNum == 3)
    ) ? 168 : 190;

    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            objective = &gBingoObjectives[5 * i + j];
            if (
                objective->type == BINGO_OBJECTIVE_STAR_TIMED
                && objective->data.starTimerObjective.course == gCurrCourseNum
                && objective->state != BINGO_STATE_FAILED_IN_THIS_COURSE
                && objective->state != BINGO_STATE_COMPLETE
            ) {
                getTimeFmtTiny(
                    buffer,
                    objective->data.starTimerObjective.maxTime - objective->data.starTimerObjective.timer
                );
                sprintf(
                    buffer2,
                    "%c%d: %s",
                    0xFA, // star icon
                    objective->data.starTimerObjective.starIndex + 1,
                    buffer
                );
                print_text_not_tiny(
                    242,
                    yOffset - (18 * (count + 1)),
                    buffer2
                );
                count++;
                strcpy(buffer, empty_string);
                strcpy(buffer2, empty_string);
            }
        }
    }
    if (count != 0) {
        print_text_not_tiny(230, yOffset, "Time remaining:\xFF");
    }
}

void bingo_print_description(char *str) {
    int last_space = 0;
    int last_space_line_chars = 0;
    int line_chars = 0;
    s32 iter = 0;
    s32 total_lines = 0;
    u8 finalDesc[150] = { 0x11, 0x28, 0x2F, 0x2F, 0xFF };

    // #define MAX_LINE_CHARS 25
    #define MAX_LINE_CHARS 22
    // TODO: This really really needs to be based on line widths
    // of each character.

    while (str[iter] != '\0') {
        line_chars++;
        // Chop the line if it's getting too long
        if (str[iter] == ' ') {
            if (line_chars >= MAX_LINE_CHARS) {
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
    if (line_chars >= MAX_LINE_CHARS) {
        finalDesc[last_space] = 0xFE;
    }
    finalDesc[iter] = 0xFF;

    // print_text_not_tiny(180, 100 + total_lines * 10, finalDesc);
    print_text_not_tiny(190, 100 + total_lines * 10, finalDesc);
}

void print_bingo_icon_alpha(s32 x, s32 y, s32 iconIndex, u8 alpha) {
    s32 rectX = x;
    s32 rectY = 224 - y;
    const u8 *const *glyphs = segmented_to_virtual(bingo_special_icon_lut);
    const u8 *texture;
    if (iconIndex == BINGO_ICON_FAILED) {
        texture = glyphs[1];
    } else if (iconIndex == BINGO_ICON_SUCCESS) {
        texture = glyphs[0];
    } else {
        texture = get_objective_info_from_icon(iconIndex)->texture;
    }


    gDPPipeSync(gDisplayListHead++);
    gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_load_tex_block);

    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, alpha);
    gSPTextureRectangle(gDisplayListHead++, rectX << 2, rectY << 2, (rectX + 16) << 2,
                        (rectY + 16) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
}

void print_bingo_icon(s32 x, s32 y, s32 iconIndex) {
    print_bingo_icon_alpha(x, y, iconIndex, gOptionSelectIconOpacity);
}


void draw_bingo_screen() {
    struct BingoObjective *objective;
    int i, j, length;
    int spacing = 35;
    enum BingoObjectiveIcon icon;
    char desc_text[300];
    char seed_print[20];
    char timestamp[40];
    char time_print[40];
    char number[5];
    char *bingo[5] = { "B", "I", "N", "G", "O" };

    if (!gBingoAllowBoardToShow) {
        return;
    }

    // Shade the screen.
    // Why was I checking this twice?
    // if (gPlayer1Controller->buttonDown & L_TRIG) {
        shade_screen_opacity(140);
    // }

    // Title.
    for (i = 0; i < 5; i++) {
        // print_text_large(6 + spacing * i, HUD_TOP_Y + 5, bingo[i]);
        print_text_large(BINGO_MIN_X + spacing * i, BINGO_MAX_Y, bingo[i]);
    }

    // Seed and time.
    sprintf(seed_print, "SEED %09d", gBingoInitialSeed);
    print_text_tiny(240, 214, seed_print);
    print_text_tiny(240, 205, "VERSION 0.11a");
    getTimeFmtPreciseTiny(timestamp, gbGlobalBingoTimer);
    sprintf(time_print, "TIME %s", timestamp);
    print_text_tiny(240, 196, time_print);

#ifndef TARGET_N64
    // Online: live standings (lockout) or the finishers so far (races),
    // stacked under the timer in the right column.
    if (network_active()) {
        char row_print[40];
        s32 rowY = 184;
        if (gbBingoMode == BINGO_MODE_LOCKOUT) {
            for (i = 0; i < NET_MAX_PLAYERS && rowY > 100; i++) {
                struct NetPlayer *p = &gNetPlayers[i];
                if (!p->active) {
                    continue;
                }
                sprintf(row_print, "%s%s %d", hud_upper(p->name),
                        p->connected ? "" : "*", net_cell_count_of_id(p->id));
                print_text_tiny(240, rowY, row_print);
                rowY -= 9;
            }
        } else {
            for (i = 0; i < network_result_count() && rowY > 100; i++) {
                const struct NetResult *r = network_result(i);
                getTimeFmtPreciseTiny(timestamp, r->frames);
                sprintf(row_print, "%d %s %s", r->place,
                        net_name_of_id(r->id), timestamp);
                print_text_tiny(240, rowY, row_print);
                rowY -= 9;
            }
        }
    }
#endif

    // Lines.
    for (i = 0; i < 4; i++) {
        // print_vertical_line(25 + spacing * i, HUD_TOP_Y - 36);
        print_vertical_line(35 + spacing * i, HUD_TOP_Y - 35);
    }
    for (i = 0; i < 4; i++) {
        // print_horizontal_line(37 + spacing * i);
        print_horizontal_line(38 + spacing * i);
    }

#ifndef TARGET_N64
    // Who claimed what: a small color chip per claiming peer next to each
    // cell's icon (several can stack in the race modes).
    if (network_active()) {
        s32 chip;
        gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
        for (i = 0; i < 5; i++) {
            for (j = 0; j < 5; j++) {
                u32 mask = gBingoCellClaimers[5 * i + j];
                s32 id;
                chip = 0;
                for (id = 0; id < 32 && chip < 4; id++) {
                    s32 color;
                    if (!(mask & ((u32) 1 << id))) {
                        continue;
                    }
                    color = network_color_of_id(id);
                    gDPSetPrimColor(gDisplayListHead++, 0, 0,
                                    gNetColorRGB[color][0],
                                    gNetColorRGB[color][1],
                                    gNetColorRGB[color][2], 230);
                    {
                        s32 rx = BINGO_MIN_X + spacing * j + 18;
                        s32 ry = (224 - (27 + spacing * i)) + chip * 6;
                        gDPFillRectangle(gDisplayListHead++, rx, ry, rx + 5, ry + 5);
                    }
                    chip++;
                }
            }
        }
    }
#endif

    // Icons.
    // This has to be a separate for-loop from the below in order
    // to save some RSP commands; namely, avoiding duplicating the one
    // that directly follows this comment.
    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            objective = &gBingoObjectives[5 * i + j];
            switch (objective->state) {
                case BINGO_STATE_COMPLETE:
                    icon = BINGO_ICON_SUCCESS;
                    break;
                case BINGO_STATE_FAILED_IN_THIS_COURSE:
                    icon = BINGO_ICON_FAILED;
                    break;
                default:
                    icon = objective->icon;
                    break;
            }
            // print_bingo_icon(11 + spacing * j, 28 + spacing * i, icon);
            print_bingo_icon(BINGO_MIN_X + spacing * j, 27 + spacing * i, icon);
        }
    }
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);

    // Subtitles.
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            objective = &gBingoObjectives[5 * i + j];
            length = strlen_tiny(objective->title);
            print_text_tiny(
                // 11 + spacing * j + 8 - (length) / 2 + 1,
                BINGO_MIN_X + spacing * j + 8 - (length) / 2 + 1,
                // 75 + spacing * (4 - i),
                74 + spacing * (4 - i),
                objective->title
            );
        }
    }

    // Icon add-ons: they tell you how many clicks/A presses/etc.
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            objective = &gBingoObjectives[5 * i + j];
            if (objective->type != BINGO_OBJECTIVE_STAR_CLICK_GAME) {
                continue;
            }
            sprintf(number, "%dx", objective->data.starClicksObjective.maxClicks);
            print_text_tiny(
                BINGO_MIN_X + spacing * j + 14,
                55 + spacing * (4 - i),
                number
            );
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
        // print_hand(14 + spacing * sBingoCursorX, 18 + spacing * sBingoCursorY);
        print_hand(24 + spacing * sBingoCursorX, 17 + spacing * sBingoCursorY);

        objective = &gBingoObjectives[5 * sBingoCursorY + sBingoCursorX];
        describe_objective(objective, desc_text);
        bingo_print_description(desc_text);
    }

    return;
}
