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
#include "engine/math_util.h"
#include "level_update.h"
#include "strcpy.h"
#include "segment2.h"
#include "ingame_menu.h"
#include "bingo_objective_info.h"
#include "segment2.h"
#include "extras/draw_util.h"
#ifndef TARGET_N64
#include <string.h>
#include "pc/network/network.h"
#include "pc/configfile.h"
#include "bingo_net.h"
#include "bingo_const.h"
#include "level_table.h"
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

// Called while the board is hidden: each open starts cursor-less, so the
// roster column isn't stuck behind a description panel from last time.
void bingo_board_cursor_reset(void) {
    sBingoCursorX = -1;
    sBingoCursorY = -1;
}

int bingostrlen(char *str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

#ifndef TARGET_N64
static const char *net_name_of_id(s32 id) {
    s32 i;
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        if (gNetPlayers[i].active && gNetPlayers[i].id == id) {
            return gNetPlayers[i].name;
        }
    }
    return "?";
}

// "1st", "2nd", "3rd", "11th"...
static const char *ordinal_suffix(s32 n) {
    if (n % 100 < 11 || n % 100 > 13) {
        if (n % 10 == 1) return "st";
        if (n % 10 == 2) return "nd";
        if (n % 10 == 3) return "rd";
    }
    return "th";
}

// The peer's ghost, which carries their whereabouts and last-heard time.
static struct NetGhost *ghost_for_name(const char *name) {
    s32 i;
    for (i = 0; i < NET_MAX_GHOSTS; i++) {
        if (gNetGhosts[i].active && strcmp(gNetGhosts[i].name, name) == 0) {
            return &gNetGhosts[i];
        }
    }
    return NULL;
}

// The second-floor area of the castle interior also holds the third
// floor; Mario's height tells them apart (3F landing sits well above
// 2000 units, the 2F rooms well below).
#define CASTLE_TIPPY_MIN_Y 2000.0f

// Community-standard whereabouts phrase for a level: "in BoB" for
// courses, "castle (tippy)" style for the castle complex (interior by
// area, the shared 2F/3F area split by height, plus outside and the
// courtyard), into out[20]; empty when unknown. The shared
// courseAbbreviations table already uses the standard forms except BOB
// (the board's HUD-font captions are caps-only, so the shared table
// can't hold the lowercase o).
static void whereabouts_for_level(s16 level, s16 area, f32 posY,
                                  char out[20]) {
    const char *part = NULL;
    s8 course;
    out[0] = '\0';
    if (level <= 0 || level >= LEVEL_COUNT) {
        return;
    }
    if (level == LEVEL_CASTLE) {
        part = area == 3 ? "basement"
             : area == 2 ? (posY > CASTLE_TIPPY_MIN_Y ? "tippy" : "upstairs")
             : "lobby";
    } else if (level == LEVEL_CASTLE_GROUNDS) {
        part = "outside";
    } else if (level == LEVEL_CASTLE_COURTYARD) {
        part = "courtyard";
    }
    if (part != NULL) {
        // Parens alone mark castle sub-areas ("castle (basement)" was
        // too wide for the column; Matt picked this form).
        snprintf(out, 20, "(%s)", part);
        return;
    }
    course = gLevelToCourseNumTable[level - 1];
    if (course == COURSE_BOB) {
        snprintf(out, 20, "in BoB");
    } else if (course > 0 && course <= 24) {
        snprintf(out, 20, "in %s", courseAbbreviations[course - 1]);
    } else {
        snprintf(out, 20, "(castle)");  // bowser arenas, odd interiors
    }
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
#define BINGO_NOTICE_TAIL_LEN 32
#define BINGO_NOTICE_LIFE 120   // frames a notice stays up (4 seconds)
#define BINGO_NOTICE_FADE 30    // fade-out tail within that life

struct BingoNotice {
    char name[BINGO_NOTICE_NAME_LEN]; // "" = no colored prefix
    char text[BINGO_NOTICE_LEN];
    char tail[BINGO_NOTICE_TAIL_LEN]; // after the icon (the cell's title)
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
                       s32 icon, const char *tail) {
    struct BingoNotice *n;
    s32 i, o;
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
    // The tail is usually a board-cell title: ASCII plus the HUD font's
    // 0xFA filled star, which the dialog font spells '*' (also a star).
    o = 0;
    if (tail != NULL) {
        for (i = 0; tail[i] != '\0' && o < BINGO_NOTICE_TAIL_LEN - 1; i++) {
            if ((u8) tail[i] == 0xFA) {
                n->tail[o++] = '*';
            } else if (tail[i] >= ' ' && tail[i] <= '~') {
                n->tail[o++] = tail[i];
            }
        }
    }
    n->tail[o] = '\0';
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
    bingo_notice_rich("", white, text, -1, NULL);
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
            bingo_notice_rich("Quate", gNetColorRGB[5], "completed",
                              BINGO_ICON_STAR, "WF\xFA""7");
            break;
        case 1:
            bingo_notice_rich("leGlitch", gNetColorRGB[1], "completed",
                              BINGO_ICON_COIN, "TTC 80");
            break;
        case 2:
            bingo_notice_rich("Boop", gNetColorRGB[3], "reconnected", -1,
                              NULL);
            break;
        case 3:
            bingo_notice("the host ended the race");
            break;
    }
}
#endif

#ifndef TARGET_N64
// One quiet line: dim strip + optional hat-colored name + text + optional
// board icon, in the dialog font. xLeft < 0 centers the line. The text and
// icon y offsets center both optically in the 19px strip (screenshot-
// tuned with Matt 2026-08-22: text -5, icon -4 relative to yText).
static void draw_quiet_line(s32 xLeft, s32 yText, const char *name,
                            const u8 nameRGB[3], const char *text,
                            const u8 textRGB[3], s32 icon, const char *tail,
                            u8 alpha) {
    s32 nameW = (name != NULL && name[0] != '\0')
                    ? get_string_width_ascii((char *) name) + 4 : 0;
    s32 textW = (text != NULL && text[0] != '\0')
                    ? get_string_width_ascii((char *) text) : 0;
    s32 iconW = icon >= 0 ? 20 : 0;
    s32 tailW = (tail != NULL && tail[0] != '\0')
                    ? get_string_width_ascii((char *) tail) + 4 : 0;
    s32 total = nameW + textW + iconW + tailW;
    s32 x = xLeft >= 0 ? xLeft : (SCREEN_WIDTH - total) / 2;

    // The dimmed strip (fillrect coords: origin top-left, y down).
    print_solid_color_quad(x - 6, SCREEN_HEIGHT - (yText + 13),
                           x + total + 6, SCREEN_HEIGHT - (yText - 6),
                           0, 0, 0, (u8) (140 * alpha / 255));

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    if (nameW > 0) {
        print_generic_string_ascii_detail(x, yText - 5, name, nameRGB[0],
                                          nameRGB[1], nameRGB[2], alpha,
                                          TRUE, 1);
    }
    if (textW > 0) {
        print_generic_string_ascii_detail(x + nameW, yText - 5, text,
                                          textRGB[0], textRGB[1], textRGB[2],
                                          alpha, TRUE, 1);
    }
    if (tailW > 0) {
        print_generic_string_ascii_detail(x + nameW + textW + iconW + 4,
                                          yText - 5, tail, textRGB[0],
                                          textRGB[1], textRGB[2], alpha,
                                          TRUE, 1);
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);

    if (icon >= 0) {
        gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
        print_bingo_icon_alpha(x + nameW + textW + 4, yText - 4, icon, alpha);
        gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
    }
}

static const u8 sQuietWhite[3] = { 255, 255, 255 };

// Celebratory register for YOUR OWN win: the same quiet strip, but the
// text color cycles through a rainbow.
static void rainbow_rgb(u8 rgb[3]) {
    u16 t = (u16) (gGlobalTimer * 0x400);
    rgb[0] = (u8) (155 + 100.0f * sins(t));
    rgb[1] = (u8) (155 + 100.0f * sins((u16) (t + 0x5555)));
    rgb[2] = (u8) (155 + 100.0f * sins((u16) (t + 0xAAAA)));
}
#endif

void draw_bingo_notices(void) {
#ifndef TARGET_N64
    s32 i;
    bingo_notice_demo_tick();
    // Dev-only: BINGO64_WIN_DEMO=1 overlays the (solo) win banner so its
    // layout can be screenshot-tested without finishing a board.
    if (getenv("BINGO64_WIN_DEMO") != NULL) {
        draw_bingo_win_screen();
    }
    while (sNoticeCount > 0
           && gGlobalTimer - sNotices[0].frame > BINGO_NOTICE_LIFE) {
        bingo_notice_drop_oldest();
    }
    if (!configBingoToasts) {
        return;  // local mute (R menu); notices still age out above
    }
    for (i = 0; i < sNoticeCount; i++) {
        // Oldest reads first: it sits highest, newest at the bottom;
        // bottom-left aligned, strips grow rightward per row.
        s32 row = sNoticeCount - 1 - i;
        struct BingoNotice *n = &sNotices[i];
        u32 age = gGlobalTimer - n->frame;
        s32 alpha = 255;
        if (age > BINGO_NOTICE_LIFE - BINGO_NOTICE_FADE) {
            alpha = 255 * (BINGO_NOTICE_LIFE - (s32) age) / BINGO_NOTICE_FADE;
            if (alpha <= 0) {
                continue;
            }
        }
        draw_quiet_line(20, 24 + 21 * row, n->name, n->rgb, n->text,
                        sQuietWhite, n->icon, n->tail, (u8) alpha);
    }
#endif
}

// Once first place is taken in an online line/blackout race, the players
// who did NOT win need to hear about it too — persistently, not as a 5s
// toast. Lockout is excluded: its verdict ends the race for everyone and
// draw_bingo_win_screen already shows it. Clears when the results clear
// (back to lobby / disconnect), or on two L presses like the win screen.
#ifndef TARGET_N64
static s32 sVerdictWinnerId = 0;
static s32 sVerdictLPresses = 0;
#endif

void bingo_race_verdict_on_l(void) {
#ifndef TARGET_N64
    if (sVerdictLPresses < 2) {
        sVerdictLPresses++;
    }
#endif
}

void draw_bingo_race_verdict(void) {
#ifndef TARGET_N64
    s32 i, winnerId = 0, myPlace, n;
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
    if (winnerId != sVerdictWinnerId) {
        // A new verdict (or none): forget dismissal state.
        sVerdictWinnerId = winnerId;
        sVerdictLPresses = 0;
    }
    if (winnerId == 0 || winnerId == network_local_id()) {
        return;  // no verdict yet, or it's ours (the win overlay's job)
    }
    if (sVerdictLPresses >= 2) {
        return;  // dismissed
    }
    myPlace = network_local_place();
    if (myPlace > 0) {
        sprintf(buf, "won - you took %d%s place", myPlace,
                ordinal_suffix(myPlace));
    } else {
        n = network_result_count() + 1;
        sprintf(buf, "won - racing for %d%s place", n, ordinal_suffix(n));
    }
    draw_quiet_line(-1, 189, net_name_of_id(winnerId),
                    gNetColorRGB[network_color_of_id(winnerId)
                                 % NET_COLOR_COUNT],
                    buf, sQuietWhite, -1, NULL, 255);
    if (sVerdictLPresses == 1) {
        draw_quiet_line(-1, 168, NULL, NULL, "press L again to dismiss",
                        sQuietWhite, -1, NULL, 255);
    }
#endif
}

#ifndef TARGET_N64
// getTimeFmtPrecise emits the HUD font's [ ] codes for the minute/second
// marks; the dialog font blanks those, so swap in its ' and . glyphs.
static void time_fmt_dialog(char *s) {
    for (; *s != '\0'; s++) {
        if (*s == '[') {
            *s = '\'';
        } else if (*s == ']') {
            *s = '.';
        }
    }
}

// The dismiss/congrats hint under the win/finish message.
static void draw_win_hint(s32 showSuperPlayer) {
    if (gbBingoShowCongratsCounter == (gbBingoShowCongratsLimit - 1)) {
        draw_quiet_line(-1, 38, NULL, NULL, "press L again to dismiss",
                        sQuietWhite, -1, NULL, 255);
    } else if (showSuperPlayer) {
        draw_quiet_line(-1, 38, NULL, NULL, "You are a super player!",
                        sQuietWhite, -1, NULL, 255);
    }
}
#endif

void draw_bingo_win_screen() {
    char timestamp[16];
    char msg[40];

#ifndef TARGET_N64
    u8 rainbow[3];
    rainbow_rgb(rainbow);
    if (network_active() && gbBingoMode == BINGO_MODE_LOCKOUT
        && network_race_winner_id() != 0) {
        // Lockout ends for the whole room at once: show the verdict in
        // the same register as the race finish ("Finished 1st in ..."),
        // with the server-timed decision, not the local clock (which
        // keeps running for everyone who didn't win).
        s32 winner = network_race_winner_id();
        s32 i, frames = -1;
        for (i = 0; i < network_result_count(); i++) {
            if (network_result(i)->id == winner) {
                frames = network_result(i)->frames;
            }
        }
        getTimeFmtPrecise(timestamp,
                          frames >= 0 ? frames : gbGlobalBingoTimer);
        time_fmt_dialog(timestamp);
        if (winner == network_local_id()) {
            // Your win: same quiet strip, celebratory rainbow text.
            sprintf(msg, "Won %d squares in %s",
                    net_cell_count_of_id(winner), timestamp);
            draw_quiet_line(-1, 60, NULL, NULL, msg, rainbow, -1, NULL, 255);
        } else {
            sprintf(msg, "won %d squares in %s",
                    net_cell_count_of_id(winner), timestamp);
            draw_quiet_line(-1, 60, net_name_of_id(winner),
                            gNetColorRGB[network_color_of_id(winner)
                                         % NET_COLOR_COUNT],
                            msg, sQuietWhite, -1, NULL, 255);
        }
        draw_win_hint(winner == network_local_id());
        return;
    }
    if (network_active()) {
        // A race finish: our official place and server-timed result.
        // Winning the race gets the rainbow; placing is told quietly.
        if (network_local_place() > 0) {
            getTimeFmtPrecise(timestamp, gbGlobalBingoTimer);
            time_fmt_dialog(timestamp);
            sprintf(msg, "Finished %d%s in %s", network_local_place(),
                    ordinal_suffix(network_local_place()), timestamp);
            draw_quiet_line(-1, 60, NULL, NULL, msg,
                            network_local_place() == 1 ? rainbow
                                                       : sQuietWhite,
                            -1, NULL, 255);
            draw_win_hint(TRUE);
        }
        // else: our finish report's placement is still a round trip
        // away — draw nothing for those frames rather than flashing
        // the solo "your time is" banner (Matt saw the flicker).
        return;
    }

    // Solo: completing the board is a win.
    getTimeFmtPrecise(timestamp, gbGlobalBingoTimer);
    time_fmt_dialog(timestamp);
    sprintf(msg, "your time is %s", timestamp);
    draw_quiet_line(-1, 60, NULL, NULL, msg, rainbow, -1, NULL, 255);
    draw_win_hint(TRUE);
#else
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
#endif
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
#ifndef TARGET_N64
    // The release version's beta number IS the protocol version;
    // client-only fixes append a patch number without touching it.
    if (NET_VERSION_PATCH) {
        sprintf(seed_print, "V1.0 BETA %d.%d", NET_PROTOCOL_VERSION,
                NET_VERSION_PATCH);
    } else {
        sprintf(seed_print, "V1.0 BETA %d", NET_PROTOCOL_VERSION);
    }
    print_text_tiny(240, 205, seed_print);
#else
    print_text_tiny(240, 205, "VERSION 0.11a");
#endif
    getTimeFmtPreciseTiny(timestamp, gbGlobalBingoTimer);
    sprintf(time_print, "TIME %s", timestamp);
    print_text_tiny(240, 196, time_print);

#ifndef TARGET_N64
    // Online: the presence roster in the right column, dialog font (PC
    // has the room — Matt's call). One row per member: the name in the
    // player's hat color (with ? appended when the connection is in
    // doubt: server dropped them, or their ghost is 2s+ silent), then
    // place + time once finished, else claim count + current course.
    // The d-pad description panel shares this column, so it takes over
    // while the cursor is up (the cursor resets when the board closes).
    if (network_active() && sBingoCursorX == -1) {
        char name_print[24];
        char detail[32];
        char course[20];
        s32 quadY[NET_MAX_PLAYERS];
        s32 nQuads = 0;
        s32 rowY = 162;
        gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
        for (i = 0; i < NET_MAX_PLAYERS && rowY > 60; i++) {
            struct NetPlayer *p = &gNetPlayers[i];
            const struct NetResult *res = NULL;
            const char *mark = "";
            s32 r;
            if (!p->active) {
                continue;
            }
            for (r = 0; r < network_result_count(); r++) {
                if (network_result(r)->id == p->id) {
                    res = network_result(r);
                    break;
                }
            }
            if (p->id != network_local_id()) {
                struct NetGhost *g = ghost_for_name(p->name);
                if (!p->connected
                    || (g != NULL
                        && gGlobalTimer - g->lastUpdateFrame > 60)) {
                    mark = "?";
                }
            }
            // Two aligned lines per player: the colored name, then a data
            // row at fixed column x's so the roster reads as a table.
            sprintf(name_print, "%s%s", p->name, mark);
            print_generic_string_ascii_detail(
                236, rowY, name_print,
                gNetColorRGB[p->color % NET_COLOR_COUNT][0],
                gNetColorRGB[p->color % NET_COLOR_COUNT][1],
                gNetColorRGB[p->color % NET_COLOR_COUNT][2], 255, TRUE, 1);
            // Data row: "<n> [] ; in BoB" while racing (the [] square
            // symbol is drawn as a literal quad below, ';' renders as
            // the dialog font's interpunct) or "1st ; 12'34.50" once
            // finished. Count right-aligned to the square, interpunct
            // column fixed so the rows read as a table.
            if (res != NULL && gbBingoMode != BINGO_MODE_LOCKOUT) {
                getTimeFmtPrecise(timestamp, res->frames);
                time_fmt_dialog(timestamp);
                sprintf(detail, "%d%s", res->place,
                        ordinal_suffix(res->place));
                print_generic_string_ascii_detail(244, rowY - 12, detail,
                                                  255, 255, 255, 255,
                                                  TRUE, 1);
                sprintf(detail, "; %s", timestamp);
                print_generic_string_ascii_detail(266, rowY - 12, detail,
                                                  255, 255, 255, 255,
                                                  TRUE, 1);
            } else {
                s32 self = p->id == network_local_id();
                course[0] = '\0';
                // Whereabouts are a room setting (your own always show).
                if (self) {
                    whereabouts_for_level(gCurrLevelNum, gCurrAreaIndex,
                                          gMarioState->pos[1], course);
                } else if (gNetShowWhereabouts) {
                    struct NetGhost *g = ghost_for_name(p->name);
                    if (g != NULL) {
                        whereabouts_for_level(g->level, g->area, g->pos[1],
                                              course);
                    }
                }
                // Progress column by the room's claim-visibility tier:
                // count + square symbol, bingo milestones ("2 bingos"),
                // or nothing at all. Your own row always shows.
                if (self || gNetClaimVis <= NET_CLAIMVIS_PROGRESS) {
                    sprintf(detail, "%d", net_cell_count_of_id(p->id));
                    print_generic_string_ascii_detail(
                        250 - get_string_width_ascii(detail), rowY - 12,
                        detail, 255, 255, 255, 255, TRUE, 1);
                    if (nQuads < NET_MAX_PLAYERS) {
                        quadY[nQuads++] = rowY - 12;
                    }
                } else if (gNetClaimVis == NET_CLAIMVIS_BINGOS) {
                    // Same table cell, star unit instead of the square:
                    // "2 * in BoB" = two bingos ('*' is the dialog ★).
                    sprintf(detail, "%d", bingo_net_bingo_count(p->id));
                    print_generic_string_ascii_detail(
                        250 - get_string_width_ascii(detail), rowY - 12,
                        detail, 255, 255, 255, 255, TRUE, 1);
                    print_generic_string_ascii_detail(253, rowY - 12, "*",
                                                      255, 255, 255, 255,
                                                      TRUE, 1);
                }
                // No interpunct here: the square symbol already breaks
                // the fields, and the two small glyphs clash side by
                // side. The whereabouts phrase carries its own "in".
                print_generic_string_ascii_detail(264, rowY - 12, course,
                                                  255, 255, 255, 255,
                                                  TRUE, 1);
            }
            rowY -= 30;
        }
        gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
        // The [] "squares" symbol after each count: a literal little
        // square, drop-shadowed like the text (quads can't be drawn
        // inside the ia-text block).
        for (i = 0; i < nQuads; i++) {
            s32 qy = 231 - quadY[i];  // fillrect y for this data row's caps
            print_solid_color_quad(255, qy + 1, 260, qy + 6, 0, 0, 0, 255);
            print_solid_color_quad(254, qy, 259, qy + 5, 255, 255, 255, 255);
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
    // Who claimed what. Only when the room shows WHICH squares (the
    // OPEN tier). Lockout: claims are exclusive, so the one owner's
    // color becomes a tile behind the whole icon (the icon on top says
    // check = yours, X = lost). Race modes: a small colored dot per
    // claiming peer, stacked below the "1x"-style add-on text so the
    // two never collide.
    if ((network_active() || bingo_net_dropped())
        && gNetClaimVis == NET_CLAIMVIS_OPEN) {
        s32 chip;
        gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
        for (i = 0; i < 5; i++) {
            for (j = 0; j < 5; j++) {
                u32 mask = gBingoCellClaimers[5 * i + j];
                s32 ix = BINGO_MIN_X + spacing * j;  // icon left edge
                s32 iy = 224 - (27 + spacing * i);   // icon top edge
                s32 id;
                if (mask == 0) {
                    continue;
                }
                if (gbBingoMode == BINGO_MODE_LOCKOUT) {
                    s32 color;
                    for (id = 0; id < 32 && !(mask & ((u32) 1 << id));
                         id++) {}
                    color = bingo_net_display_color(id);
                    // Translucent enough that the X stays readable even
                    // when the owner's hat color is also red.
                    gDPSetPrimColor(gDisplayListHead++, 0, 0,
                                    gNetColorRGB[color][0],
                                    gNetColorRGB[color][1],
                                    gNetColorRGB[color][2], 110);
                    gDPFillRectangle(gDisplayListHead++, ix - 1, iy - 1,
                                     ix + 17, iy + 17);
                    continue;
                }
                chip = 0;
                for (id = 0; id < 32 && chip < 4; id++) {
                    s32 color;
                    if (!(mask & ((u32) 1 << id))) {
                        continue;
                    }
                    color = bingo_net_display_color(id);
                    gDPSetPrimColor(gDisplayListHead++, 0, 0,
                                    gNetColorRGB[color][0],
                                    gNetColorRGB[color][1],
                                    gNetColorRGB[color][2], 230);
                    {
                        // Upper-left of the cell: the gap between the
                        // grid line and the icon, top-aligned -- clear
                        // of the "1x" add-on text (top-right) and of
                        // most captions.
                        s32 rx = ix - 5;  // flush left of the icon,
                                          // clear of the grid bar
                        s32 ry = iy + chip * 6;
                        // A 5x5 square with its corners knocked off
                        // reads as a round dot at this size.
                        gDPFillRectangle(gDisplayListHead++,
                                         rx, ry + 1, rx + 5, ry + 4);
                        gDPFillRectangle(gDisplayListHead++,
                                         rx + 1, ry, rx + 4, ry + 5);
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
#ifndef TARGET_N64
                    // Lockout: a square an opponent took is lost to us,
                    // so the green check (which reads "done by you")
                    // becomes the red X. A pending local claim has no
                    // owner bits yet and stays a check until the server
                    // hands the square to whoever won the race for it.
                    if (gbBingoMode == BINGO_MODE_LOCKOUT
                        && (network_active() || bingo_net_dropped())) {
                        u32 owners = gBingoCellClaimers[5 * i + j];
                        if (owners != 0
                            && !(owners
                                 & ((u32) 1 << bingo_net_display_id()))) {
                            icon = BINGO_ICON_FAILED;
                        }
                    }
#endif
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
