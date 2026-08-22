#ifndef _BINGO_UI_H
#define _BINGO_UI_H

extern s8 gBingoAllowBoardToShow;
extern s8 gForceDrawBingoScreen;

void print_bingo_icon(s32 x, s32 y, s32 iconIndex);
void print_bingo_icon_alpha(s32 x, s32 y, s32 iconIndex, u8 alpha);
void draw_bingo_hud_timer(void);
void draw_bingo_screen(void);
void draw_bingo_win_screen(void);

// Toast feed for online events ("Quate completed [icon] TTC 80", "Boop
// left"). Toasts expire after 4s; rich toasts open with a hat-colored name
// and can carry a board-icon glyph (-1 = none) plus a tail after it
// (usually the cell's title; NULL = none).
void bingo_notice(const char *text);
void bingo_notice_rich(const char *name, const u8 rgb[3], const char *text,
                       s32 icon, const char *tail);
void draw_bingo_notices(void);  // HUD hook (bottom-center subtitles)
void draw_bingo_race_verdict(void);  // HUD hook (persistent, top center)
// Newest notice and its age in frames, or NULL: the lobby status line
// shows fresh notices for players who are back at the file select.
const char *bingo_notice_latest(u32 *ageFrames);

#endif /* _BINGO_UI_H */