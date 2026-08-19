#ifndef _BINGO_UI_H
#define _BINGO_UI_H

extern s8 gBingoAllowBoardToShow;
extern s8 gForceDrawBingoScreen;

void print_bingo_icon(s32 x, s32 y, s32 iconIndex);
void print_bingo_icon_alpha(s32 x, s32 y, s32 iconIndex, u8 alpha);
void draw_bingo_hud_timer(void);
void draw_bingo_screen(void);
void draw_bingo_win_screen(void);

// Toast queue for online events ("MATT LEFT", "HOST ENDED THE RACE").
// bingo_notice uppercases and truncates; notices expire after 5s.
void bingo_notice(const char *text);
void draw_bingo_notices(void);  // HUD hook (top-left stack)
void draw_bingo_race_verdict(void);  // HUD hook (persistent, top center)
// Newest notice and its age in frames, or NULL: the lobby status line
// shows fresh notices for players who are back at the file select.
const char *bingo_notice_latest(u32 *ageFrames);

#endif /* _BINGO_UI_H */