#include <ultra64.h>

#include "sm64.h"
#include "game.h"
#include "mario.h"
#include "memory.h"
#include "save_file.h"
#include "main.h"
#include "engine/surface_collision.h"
#include "geo_misc.h"
#include "ingame_menu.h"
#include "segment2.h"
#include "level_update.h"
#include "bingo.h"
#include "geo_misc.h"
#include "print.h"

/**
* This file handles printing and formatting the colorful text that
* appears when printing things such as "PRESS START".
*/

struct TextLabel {
    u32 x;
    u32 y;
    s16 length;
    u32 size;
    char buffer[50];
};

extern u8 seg2_dl_0200EC60[];
extern u8 seg2_dl_0200ECC8[];
extern u8 seg2_dl_0200EC98[];

/**
* Stores the text to be rendered on screen
* and how they are to be rendered.
*/
struct TextLabel *sTextLabels[520];

s16 sTextLabelsCount = 0;

/**
* Returns n to the exponent power, only for non-negative powers.
*/
s32 int_pow(s32 n, s32 exponent) {
    s32 result = 1;
    s32 i;

    for (i = 0; i < exponent; i++) {
        result = n * result;
    }

    return result;
}

/**
* Formats an integer n for print by fitting it to width, prefixing with a negative,
* and converting the base.
*/
void format_integer(s32 n, s32 base, char *dest, s32 *totalLength, u8 width, s8 zeroPad) {
    u32 powBase;
    s32 numDigits = 0;
    s32 i;
    s32 len = 0;
    s8 digit;
    s8 negative = FALSE;
    char pad;

    if (zeroPad == TRUE) {
        pad = '0';
    } else {
        pad = -1;
    }

    if (n != 0) {
        // Formats a negative number for negative prefix.
        if (n < 0) {
            n = -n;
            negative = TRUE;
        }

        // Increments the number of digits until length is long enough.
        while (1) {
            powBase = int_pow(base, numDigits);

            if (powBase > (u32) n) {
                break;
            }

            numDigits++;
        }

        // Add leading pad to fit width.
        if (width > numDigits) {
            for (len = 0; len < width - numDigits; len++) {
                dest[len] = pad;
            }

            // Needs 1 length to print negative prefix.
            if (negative == TRUE) {
                len--;
            }
        }

        // Use 'M' prefix to indicate negative numbers.
        if (negative == TRUE) {
            dest[len] = 'M';
            len++;
        }

        // Transfer the digits into the proper base.
        for (i = numDigits - 1; i >= 0; i--) {
            powBase = int_pow(base, i);
            digit = n / powBase;

            // FIXME: Why doesn't [] match?
            if (digit < 10) {
                *(dest + len + numDigits - 1 - i) = digit + '0';
            } else {
                *(dest + len + numDigits - 1 - i) = digit + '7';
            }

            n -= digit * powBase;
        }
    } else // n is zero.
    {
        numDigits = 1;
        if (width > numDigits) {
            for (len = 0; len < width - numDigits; len++) {
                dest[len] = pad;
            }
        }
        dest[len] = '0';
    }

    *totalLength += numDigits + len;
}

/**
* Determines the width of the number for printing, writing to 'width'.
* Additionally, this determines if a number should be zero-padded,
* writing to 'zeroPad'.
*/
void parse_width_field(const char *str, s32 *srcIndex, u8 *width, s8 *zeroPad) {
    s8 digits[12]; // unknown length
    s8 digitsLen = 0;
    s16 i;

    // If first character is 0, then the string should be zero padded.
    if (str[*srcIndex] == '0') {
        *zeroPad = TRUE;
    }

    // Read width digits up until the 'd' or 'x' format specifier.
    while (str[*srcIndex] != 'd' && str[*srcIndex] != 'x') {
        digits[digitsLen] = str[*srcIndex] - '0';

        if (digits[digitsLen] < 0 || digits[digitsLen] >= 10) // not a valid digit
        {
            *width = 0;
            return;
        }

        digitsLen++;
        (*srcIndex)++;
    }

    // No digits
    if (digitsLen == 0) {
        return;
    }

    // Sum the digits to calculate the total width.
    for (i = 0; i < digitsLen - 1; i++) {
        *width = *width + digits[i] * ((digitsLen - i - 1) * 10);
    }

    *width = *width + digits[digitsLen - 1];
}

/**
* Takes a number, finds the intended base, formats the number, and prints it
* at the given X & Y coordinates.
*
* Warning: this fails on too large numbers, because format_integer has bugs
* related to overflow. For romhacks, prefer sprintf + print_text.
*/
void print_text_fmt_int(s32 x, s32 y, const char *str, s32 n) {
    char c = 0;
    s8 zeroPad = FALSE;
    u8 width = 0;
    s32 base = 0;
    s32 len = 0;
    s32 srcIndex = 0;

    // Don't continue if there is no memory to do so.
    if ((sTextLabels[sTextLabelsCount] = mem_pool_alloc(gEffectsMemoryPool,
                                                        sizeof(struct TextLabel))) == NULL) {
        return;
    }

    sTextLabels[sTextLabelsCount]->x = x;
    sTextLabels[sTextLabelsCount]->y = y;

    c = str[srcIndex];

    while (c != 0) {
        if (c == '%') {
            srcIndex++;

            parse_width_field(str, &srcIndex, &width, &zeroPad);

            if (str[srcIndex] != 'd' && str[srcIndex] != 'x') {
                break;
            }
            if (str[srcIndex] == 'd') {
                base = 10;
            }
            if (str[srcIndex] == 'x') {
                base = 16;
            }

            srcIndex++;

            format_integer(n, base, sTextLabels[sTextLabelsCount]->buffer + len, &len, width, zeroPad);
        } else // straight copy
        {
            sTextLabels[sTextLabelsCount]->buffer[len] = c;
            len++;
            srcIndex++;
        }
        c = str[srcIndex];
    }

    sTextLabels[sTextLabelsCount]->length = len;
    sTextLabels[sTextLabelsCount]->size = 0;
    sTextLabelsCount++;
}

/**
* Prints text in the colorful lettering at given X, Y coordinates.
*/
void print_text(s32 x, s32 y, const char *str) {
    char c = 0;
    s32 length = 0;
    s32 srcIndex = 0;

    // Don't continue if there is no memory to do so.
    if ((sTextLabels[sTextLabelsCount] = mem_pool_alloc(gEffectsMemoryPool,
                                                        sizeof(struct TextLabel))) == NULL) {
        return;
    }

    sTextLabels[sTextLabelsCount]->x = x;
    sTextLabels[sTextLabelsCount]->y = y;

    c = str[srcIndex];

    // Set the array with the text to print while finding length.
    while (c != 0) {
        sTextLabels[sTextLabelsCount]->buffer[length] = c;
        length++;
        srcIndex++;
        c = str[srcIndex];
    }
    sTextLabels[sTextLabelsCount]->length = length;
    sTextLabels[sTextLabelsCount]->size = 0;
    sTextLabelsCount++;
}

void print_text_tiny(int x, int y, const char *str) {
    char c = 0;
    int len = 0;
    int srcIndex = 0;

    if ((sTextLabels[sTextLabelsCount] = (struct TextLabel *) mem_pool_alloc(gEffectsMemoryPool, 60)) == NULL) {
        return;
    }

    sTextLabels[sTextLabelsCount]->x = x;
    sTextLabels[sTextLabelsCount]->y = y;

    c = str[srcIndex];
    while (c != 0) {
        sTextLabels[sTextLabelsCount]->buffer[len] = c;
        len++;
        srcIndex++;
        c = str[srcIndex];
    }
    sTextLabels[sTextLabelsCount]->length = len;
    sTextLabels[sTextLabelsCount]->size = -1;
    sTextLabelsCount++;
}

void print_text_not_tiny(int x, int y, const char *str) {
    char c = 0;
    int len = 0;
    int srcIndex = 0;

    if ((sTextLabels[sTextLabelsCount] = (struct TextLabel *) mem_pool_alloc(gEffectsMemoryPool, 150))
        == NULL) {
        return;
    }

    sTextLabels[sTextLabelsCount]->x = x;
    sTextLabels[sTextLabelsCount]->y = y;

    c = str[srcIndex];
    while (c != 0) {
        sTextLabels[sTextLabelsCount]->buffer[len] = c;
        len++;
        srcIndex++;
        c = str[srcIndex];
    }
    sTextLabels[sTextLabelsCount]->length = len;
    sTextLabels[sTextLabelsCount]->size = 3;
    sTextLabelsCount++;
}

void print_text_large(int x, int y, const char *str) {
    char c = 0;
    int len = 0;
    int srcIndex = 0;

    if ((sTextLabels[sTextLabelsCount] = (struct TextLabel *) mem_pool_alloc(gEffectsMemoryPool, 60)) == NULL) {
        return;
    }

    sTextLabels[sTextLabelsCount]->x = x;
    sTextLabels[sTextLabelsCount]->y = y;

    c = str[srcIndex];
    while (c != 0) {
        sTextLabels[sTextLabelsCount]->buffer[len] = c;
        len++;
        srcIndex++;
        c = str[srcIndex];
    }
    sTextLabels[sTextLabelsCount]->length = len;
    sTextLabels[sTextLabelsCount]->size = 1;
    sTextLabelsCount++;
}

void print_vertical_line(int x, int y) {
    char c = 0;
    int len = 0;

    if ((sTextLabels[sTextLabelsCount] = (struct TextLabel *) mem_pool_alloc(gEffectsMemoryPool, 60)) == NULL) {
        return;
    }

    sTextLabels[sTextLabelsCount]->x = x + 8;
    sTextLabels[sTextLabelsCount]->y = y;

    sTextLabels[sTextLabelsCount]->buffer[len] = 'L';
    sTextLabels[sTextLabelsCount]->length = 1;
    sTextLabels[sTextLabelsCount]->size = 2;
    sTextLabelsCount++;
}

void print_hand(int x, int y) {
    char c = 0;
    int len = 0;

    if ((sTextLabels[sTextLabelsCount] = (struct TextLabel *) mem_pool_alloc(gEffectsMemoryPool, 60)) == NULL) {
        return;
    }

    sTextLabels[sTextLabelsCount]->x = x + 8;
    sTextLabels[sTextLabelsCount]->y = y;

    sTextLabels[sTextLabelsCount]->buffer[len] = '.';
    sTextLabels[sTextLabelsCount]->length = 1;
    sTextLabels[sTextLabelsCount]->size = 0;
    sTextLabelsCount++;
}

void print_horizontal_line(int y) {
    char c = 0;
    int len = 0;

    if ((sTextLabels[sTextLabelsCount] = (struct TextLabel *) mem_pool_alloc(gEffectsMemoryPool, 60)) == NULL) {
        return;
    }

    sTextLabels[sTextLabelsCount]->x = 0;
    sTextLabels[sTextLabelsCount]->y = y;

    // sTextLabels[sTextLabelsCount]->buffer[len] = 'L';
    sTextLabels[sTextLabelsCount]->buffer[len] = '\x1F';
    sTextLabels[sTextLabelsCount]->length = 1;
    sTextLabels[sTextLabelsCount]->size = -2;
    sTextLabelsCount++;
}

/**
* Prints text in the colorful lettering centered
* at given X, Y coordinates.
*/
void print_text_centered(s32 x, s32 y, const char *str) {
    char c = 0;
    UNUSED s8 unused1 = 0;
    UNUSED s32 unused2 = 0;
    s32 length = 0;
    s32 srcIndex = 0;

    // Don't continue if there is no memory to do so.
    if ((sTextLabels[sTextLabelsCount] = mem_pool_alloc(gEffectsMemoryPool,
                                                        sizeof(struct TextLabel))) == NULL) {
        return;
    }

    c = str[srcIndex];

    // Set the array with the text to print while finding length.
    while (c != 0) {
        sTextLabels[sTextLabelsCount]->buffer[length] = c;
        length++;
        srcIndex++;
        c = str[srcIndex];
    }
    sTextLabels[sTextLabelsCount]->length = length;
    sTextLabels[sTextLabelsCount]->size = 0;
    sTextLabels[sTextLabelsCount]->x = x - length * 12 / 2;
    sTextLabels[sTextLabelsCount]->y = y;
    sTextLabelsCount++;
}

/**
 * Converts a char into the proper colorful glyph for the char.
 */
s8 char_to_glyph_index(char a) {
    if (a >= 'A' && a <= 'Z') {
        return a - 55;
    }
    if (a >= 'a' && a <= 'z') {
        return a - 87;
    }
    if (a >= '0' && a <= '9') {
        return a - 48;
    }
    if (a == ' ') {
        return -1;
    }
    if (a == '!') {
        return 36;
    }
    if (a == '#') {
        return 37;
    }
    if (a == '?') {
        return 38;
    }
    if (a == '&') {
        return 39;
    }
    if (a == '%') {
        return 40;
    }
    // fill in this gap to add more icons
    if (a == '\x1F') {
        return 44;
    }
    if (a == '"') {
        return 45;
    }
    if (a == '$') {
        return 46;
    }
    if (a == '\'') {
        return 47;
    }
    if (a == '(') {
        return 48;
    }
    if (a == ')') {
        return 49;
    }
    if (a == '*') {
        return 50;
    }
    if (a == '+') {
        return 51;
    }
    if (a == ',') {
        return 52;
    }
    if (a == '-') {
        return 53;
    }
    if (a == '.') {
        return 54;
    }
    if (a == '/') {
        return 55;
    }
    if (a == '[') {
        return 56;
    }
    if (a == ']') {
        return 57;
    }
    // can also add icons here if you increase the size of seg2_hud_lut
    if (a == '^') {
        return 58;
    }
    if (a == '_') {
        return 59;
    }
    if (a == '`') {
        return 60;
    }
    if (a == ':') {
        return 61;
    }
    if (a == '\x1E') {
        return 62;
    }
    return -1;
}

/**
 * Adds an individual glyph to be rendered.
 */
void add_glyph_texture(s8 glyphIndex) {
    const u8 *const *glyphs = segmented_to_virtual(main_hud_lut);

    gDPPipeSync(gDisplayListHead++);
    gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, glyphs[glyphIndex]);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_load_tex_block);
}

/**
 * Clips textrect into the boundaries defined.
 */
void clip_to_bounds(s32 *x, s32 *y) {
    if (*x < TEXRECT_MIN_X) {
        *x = TEXRECT_MIN_X;
    }

    if (*x > TEXRECT_MAX_X) {
        *x = TEXRECT_MAX_X;
    }

    if (*y < TEXRECT_MIN_Y) {
        *y = TEXRECT_MIN_Y;
    }

    if (*y > TEXRECT_MAX_Y) {
        *y = TEXRECT_MAX_Y;
    }
}

/**
 * Renders the glyph that's set at the given position.
 */
void render_textrect(s32 x, s32 y, s32 pos) {
    s32 rectBaseX = x + pos * 12;
    s32 rectBaseY = 224 - y;
    s32 rectX;
    s32 rectY;

    clip_to_bounds(&rectBaseX, &rectBaseY);
    rectX = rectBaseX;
    rectY = rectBaseY;
    gSPTextureRectangle(gDisplayListHead++, rectX << 2, rectY << 2, (rectX + 16) << 2,
                        (rectY + 16) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
}

static void render_large_text(int x, int y, int pos) {
    int sp34 = x + pos * 24;
    int sp30 = 224 - y;
    int sp2C;
    int sp28;

    clip_to_bounds(&sp34, &sp30);
    sp2C = sp34;
    sp28 = sp30;
    gSPTextureRectangle(gDisplayListHead++, sp2C << 2, sp28 << 2, (sp2C + 32) << 2, (sp28 + 32) << 2, 0,
                        0, 0, 512, 512);
}

static void render_vertical_line(int x, int y, int pos) {
    int sp34 = x + 1 + pos * 12;
    int sp30 = 226 - y;
    int sp2C;
    int sp28;
    int height = 170;

    clip_to_bounds(&sp34, &sp30);
    sp2C = sp34;
    sp28 = sp30;

    gDPSetCombineLERP(gDisplayListHead++, 0, 0, 0, ENVIRONMENT, 0, 0, 0, 1, 0, 0, 0, ENVIRONMENT, 0, 0,
                      0, 1);

    gDPSetEnvColor(gDisplayListHead++, 127, 127, 127, 0);
    gSPTextureRectangle(gDisplayListHead++, sp2C << 2, sp28 << 2, (sp2C + 6) << 2, (sp28 + height) << 2,
                        0, 0, 0, 32, 32);

    gDPSetEnvColor(gDisplayListHead++, 200, 200, 200, 0);
    gSPTextureRectangle(gDisplayListHead++, sp2C << 2, sp28 << 2, (sp2C + 6 - 1) << 2,
                        (sp28 + height - 1) << 2, 0, 0, 0, 32, 32);

    gDPSetCombineLERP(gDisplayListHead++, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0,
                      TEXEL0);
}

u8 tiny_text_convert_ascii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (c - 0x37);
    }

    if (c >= 'a' && c <= 'z') {
        return (c - 0x3D);
    }

    if (c >= '0' && c <= '9') {
        return (c - 0x30);
    }

    if (c == ' ') {
        return 0x9E;
    }
    if (c == '\'') {
        return 0x3E;
    }
    if (c == '!') {
        return 0xF2;
    }
    if (c == '-') {
        return 0x9F;
    }
    if (c == '.') {
        // return 0x24;
        return 0x3F;
    }
    if (c == ',') {
        return 0x6F;
    }
    if (c == '\n') {
        return 0xFE;
    }
    if (c == ':') {
        return 0xE6;
    }
    if (c == '(') {
        return 0xE1;
    }
    if (c == ')') {
        return 0xE3;
    }

    return c; // lol idk
}

static void render_tiny_text(struct TextLabel *text) {
    int i;

    for (i = 0; i < text->length; i++) {
        text->buffer[i] = tiny_text_convert_ascii(text->buffer[i]);
    }
    text->buffer[text->length] = 0xFF;
    print_generic_string_but_tiny(text->x, text->y, text->buffer);
}

static void render_not_tiny_text(struct TextLabel *text) {
    int i;

    for (i = 0; i < text->length; i++) {
        text->buffer[i] = tiny_text_convert_ascii(text->buffer[i]);
    }
    text->buffer[text->length] = 0xFF;
    print_generic_string(text->x, text->y, text->buffer);
}

static void render_horizontal_line(int x, int y, int pos) {
    int sp34 = x + pos * 12;
    int sp30 = 226 - y;
    int sp2C;
    int sp28;
    int length = 167;

    clip_to_bounds(&sp34, &sp30);
    sp2C = sp34;
    sp28 = sp30;
    // gSPTextureRectangleFlip(gDisplayListHead++, sp2C << 2, sp28 << 2,
    //                         (sp2C + length) << 2, (sp28 + 15) << 2, 0, 0, 0, 4096, 0x10);
    gDPSetCombineLERP(gDisplayListHead++, 0, 0, 0, ENVIRONMENT, 0, 0, 0, 1, 0, 0, 0, ENVIRONMENT, 0, 0,
                      0, 1);

    gDPSetEnvColor(gDisplayListHead++, 127, 127, 127, 0);
    gSPTextureRectangle(gDisplayListHead++, sp2C << 2, sp28 << 2, (sp2C + length) << 2, (sp28 + 6) << 2,
                        0, 0, 0, 1024, 1024);

    gDPSetEnvColor(gDisplayListHead++, 200, 200, 200, 0);
    gSPTextureRectangle(gDisplayListHead++, sp2C + 1 << 2, sp28 + 1 << 2, (sp2C + length - 1) << 2,
                        (sp28 + 6 - 1) << 2, 0, 0, 0, 1024, 1024);
    gDPSetCombineLERP(gDisplayListHead++, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0,
                      TEXEL0);
}

/**
* Renders the text in sTextLabels on screen at the proper locations by iterating
* a for loop.
*/
void render_text_labels(void) {
    s32 i;
    s32 j;
    s8 glyphIndex;
    Mtx *mtx;

    // TODO: add gbBingoCompleted check.
    if (gPlayer1Controller->buttonDown & L_TRIG && gHudDisplay.flags != HUD_DISPLAY_NONE) {
        shade_screen();
    }

    if (sTextLabelsCount == 0) {
        return;
    }

    mtx = alloc_display_list(sizeof(*mtx));

    if (mtx == NULL) {
        sTextLabelsCount = 0;
        return;
    }

    //!!!! This is incredibly fragile!
    // It assumes the not tiny text comes before the tiny text in the order of
    // a frame. Look here first if something breaks.
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
    for (i = 0; i < sTextLabelsCount; i++) {
        if (sTextLabels[i]->size == 3) {
            render_not_tiny_text(sTextLabels[i]);
        }
    }
    // dl_add_new_scale_matrix(MENU_MTX_PUSH, 0.5, 0.5, 1.0f);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
    gSPDisplayList(gDisplayListHead++, my_fancy_custom_text_dl);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);

    for (i = 0; i < sTextLabelsCount; i++) {
        if (sTextLabels[i]->size == -1) {
            render_tiny_text(sTextLabels[i]);
        }
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);

    guOrtho(mtx, 0.0f, 320.0f, 0.0f, 240.0f, -10.0f, 10.0f, 1.0f);
    gSPPerspNormalize((Gfx *) (gDisplayListHead++), 0x0000FFFF);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_LOAD);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);

    for (i = 0; i < sTextLabelsCount; i++) {
        for (j = 0; j < sTextLabels[i]->length; j++) {
            glyphIndex = char_to_glyph_index(sTextLabels[i]->buffer[j]);

            if (glyphIndex != GLYPH_SPACE) {
#ifdef VERSION_EU
                // Beta Key was removed by EU, so glyph slot reused.
                // This produces a colorful Ü.
                if (glyphIndex == GLYPH_BETA_KEY) {
                    add_glyph_texture(GLYPH_U);
                    render_textrect(sTextLabels[i]->x, sTextLabels[i]->y, j);

                    add_glyph_texture(GLYPH_UMLAUT);
                    render_textrect(sTextLabels[i]->x, sTextLabels[i]->y + 3, j);
                } else {
                    add_glyph_texture(glyphIndex);
                    render_textrect(sTextLabels[i]->x, sTextLabels[i]->y, j);
                }
#else
                add_glyph_texture(glyphIndex);
                if (sTextLabels[i]->size == -2) {
                    render_horizontal_line(sTextLabels[i]->x, sTextLabels[i]->y, j);
                } else if (sTextLabels[i]->size == 1) {
                    render_large_text(sTextLabels[i]->x, sTextLabels[i]->y, j);
                } else if (sTextLabels[i]->size == 2) {
                    render_vertical_line(sTextLabels[i]->x, sTextLabels[i]->y, j);
                } else if (sTextLabels[i]->size == 0) {
                    render_textrect(sTextLabels[i]->x, sTextLabels[i]->y, j);
                    // func_802D605C
#endif
                }
            }
        }

        mem_pool_free(gEffectsMemoryPool, (void *)sTextLabels[i]);
    }

    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);

    sTextLabelsCount = 0;
}
