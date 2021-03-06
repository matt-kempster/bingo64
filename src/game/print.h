#ifndef _PRINT_H
#define _PRINT_H

#include "types.h"

#define TEXRECT_MIN_X 10
#define TEXRECT_MAX_X 300
#define TEXRECT_MIN_Y 5
#define TEXRECT_MAX_Y 220

extern u8 gOptionSelectIconOpacity;

extern void print_text_large(int, int, const char *);
extern void print_text_tiny(int, int, const char *);
extern void print_text_not_tiny(int, int, const char *);
extern void print_vertical_line(int, int);
extern void print_horizontal_line(int);
extern void print_hand(int x, int y);
extern u8 tiny_text_convert_ascii(char);

#define GLYPH_SPACE           -1
#define GLYPH_U               30
#define GLYPH_EXCLAMATION_PNT 36
#define GLYPH_TWO_EXCLAMATION 37
#define GLYPH_QUESTION_MARK   38
#define GLYPH_AMPERSAND       39
#define GLYPH_PERCENT         40
#define GLYPH_MULTIPLY        50
#define GLYPH_COIN            51
#define GLYPH_MARIO_HEAD      52
#define GLYPH_STAR            53
#define GLYPH_PERIOD          54
#define GLYPH_BETA_KEY        55
#define GLYPH_APOSTROPHE      56
#define GLYPH_DOUBLE_QUOTE    57
#define GLYPH_UMLAUT          58

extern void print_text_fmt_int(s32 x, s32 y, const char *str, s32 n);
extern void print_text(s32 x, s32 y, const char *str);
extern void print_text_alpha(s32 x, s32 y, const char *str, u8);
extern void print_text_centered(s32 x, s32 y, const char *str);
extern void render_text_labels(void);

#endif /* _PRINT_H */
