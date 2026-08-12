// The ONLINE lobby screen inside file select (PC only).
// Reachable from the seed screen's ONLINE button; fields are edited with
// the d-pad/arrows + A, text fields grab the keyboard (text_input.h).

#include "online_lobby.h"

#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "audio/external.h"
#include "game/bingo.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/segment2.h"
#include "sm64.h"
#include "sounds.h"

#include "pc/configfile.h"
#include "pc/controller/text_input.h"
#include "pc/network/network.h"

enum LobbyField {
    FIELD_NAME,
    FIELD_SERVER,
    FIELD_ROOM,
    FIELD_COLOR,
    FIELD_TYPE,
    FIELD_CONNECT,
    FIELD_READY,
    FIELD_COUNT
};

static s32 sSel = FIELD_CONNECT;
static s32 sPublic = 0;

static const char *sColorNames[NET_COLOR_COUNT] = {
    "RED", "GREEN", "BLUE", "YELLOW", "PURPLE", "PINK", "CYAN", "WHITE",
};
#define sColorRGB gNetColorRGB

// From file_select.c: the locally entered seed, offered to the server when
// our join creates the room.
extern s32 gBingoSeedIsSet;
u32 get_seed(void);

// ASCII -> SM64 dialog/menu charmap. Unmappable characters become spaces.
static void ascii_to_menu(u8 *dst, const char *src, s32 dstSize) {
    s32 i;
    for (i = 0; src[i] != '\0' && i < dstSize - 1; i++) {
        char c = src[i];
        u8 out = 0x9E;  // space
        if (c >= '0' && c <= '9') {
            out = c - '0';
        } else if (c >= 'A' && c <= 'Z') {
            out = 0x0A + (c - 'A');
        } else if (c >= 'a' && c <= 'z') {
            out = 0x24 + (c - 'a');
        } else if (c == '.') {
            out = 0x3F;
        } else if (c == '-') {
            out = 0x9F;
        } else if (c == ':') {
            out = 0xE6;
        } else if (c == '\'') {
            out = 0x3E;
        }
        dst[i] = out;
    }
    dst[i] = 0xFF;
}

static void print_ascii(s16 x, s16 y, const char *str) {
    u8 buf[64];
    ascii_to_menu(buf, str, sizeof(buf));
    print_generic_string(x, y, buf);
}

// May we still edit connection settings? Only before connecting.
static s32 settings_editable(void) {
    enum NetState st = network_state();
    return st == NET_STATE_OFF || st == NET_STATE_ERROR;
}

static void activate_field(s32 field) {
    switch (field) {
        case FIELD_NAME:
            if (settings_editable()) {
                text_input_start(configNetName, sizeof(configNetName));
            }
            break;
        case FIELD_SERVER:
            if (settings_editable()) {
                text_input_start(configNetServer, sizeof(configNetServer));
            }
            break;
        case FIELD_ROOM:
            if (settings_editable()) {
                text_input_start(configNetRoom, sizeof(configNetRoom));
            }
            break;
        case FIELD_COLOR:
            if (settings_editable()) {
                configNetColor = (configNetColor + 1) % NET_COLOR_COUNT;
            }
            break;
        case FIELD_TYPE:
            if (settings_editable()) {
                sPublic ^= 1;
            }
            break;
        case FIELD_CONNECT:
            if (settings_editable()) {
                network_connect(configNetServer, configNetRoom, configNetName,
                                (s32) configNetColor, sPublic,
                                gBingoSeedIsSet ? get_seed() : 0);
            } else {
                network_disconnect();
            }
            break;
        case FIELD_READY:
            network_set_ready(!network_local_ready());
            break;
    }
}

s32 online_lobby_handle_input(void) {
    u16 pressed;

    if (text_input_active()) {
        if (text_input_take_finished()) {
            text_input_stop();
        }
        return 0;
    }

    pressed = gPlayer3Controller->buttonPressed;
    if (pressed & (B_BUTTON | START_BUTTON)) {
        return 1;
    }
    if (pressed & (U_JPAD | U_CBUTTONS)) {
        sSel = (sSel + FIELD_COUNT - 1) % FIELD_COUNT;
        play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
    } else if (pressed & (D_JPAD | D_CBUTTONS)) {
        sSel = (sSel + 1) % FIELD_COUNT;
        play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
    } else if (pressed & A_BUTTON) {
        activate_field(sSel);
    }
    return 0;
}

#define LOBBY_LEFT_X   20
#define LOBBY_VALUE_X  78
#define LOBBY_RIGHT_X  190
#define LOBBY_TOP_Y    172
#define LOBBY_ROW_H    15

static s32 field_row_y(s32 field) {
    // The read-only MODE line sits between TYPE and CONNECT.
    s32 row = field + (field >= FIELD_CONNECT ? 1 : 0);
    return LOBBY_TOP_Y - row * LOBBY_ROW_H;
}

#define LOBBY_MODE_ROW_Y (LOBBY_TOP_Y - (FIELD_TYPE + 1) * LOBBY_ROW_H)

// The room's game mode: the creator sets it on the OPTION screen and the
// server tells everyone else.
static const char *mode_name(void) {
    switch (gbBingoMode) {
        case BINGO_MODE_LINE_2:   return "2 BINGOS";
        case BINGO_MODE_LINE_3:   return "3 BINGOS";
        case BINGO_MODE_BLACKOUT: return "BLACKOUT";
        case BINGO_MODE_LOCKOUT:  return "LOCKOUT";
        default:                  return "1 BINGO";
    }
}

static void draw_selection_highlight(u8 alpha) {
    s32 y = field_row_y(sSel);
    // Fill rects use top-down screen coords; text y counts from the bottom.
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 38, 38, 38, MIN(alpha, 150));
    gDPFillRectangle(gDisplayListHead++, LOBBY_LEFT_X - 3, 240 - y - 13,
                     LOBBY_RIGHT_X - 15, 240 - y + 2);
}

static const char *connect_label(void) {
    switch (network_state()) {
        case NET_STATE_OFF:
        case NET_STATE_ERROR:
            return "CONNECT";
        case NET_STATE_CONNECTING:
            return "CANCEL";
        default:
            return "DISCONNECT";
    }
}

static void status_text(char *buf, s32 size) {
    s32 i, ready = 0, total = 0;
    switch (network_state()) {
        case NET_STATE_OFF:
            snprintf(buf, size, "NOT CONNECTED");
            break;
        case NET_STATE_CONNECTING:
            snprintf(buf, size, "CONNECTING...");
            break;
        case NET_STATE_LOBBY:
            for (i = 0; i < NET_MAX_PLAYERS; i++) {
                if (gNetPlayers[i].active) {
                    total++;
                    ready += gNetPlayers[i].ready;
                }
            }
            snprintf(buf, size, "%d OF %d READY", ready, total);
            break;
        case NET_STATE_COUNTDOWN:
            snprintf(buf, size, "STARTING IN %d", (network_countdown_frames() + 29) / 30);
            break;
        case NET_STATE_RACING:
            snprintf(buf, size, "GO. PICK A FILE");
            break;
        case NET_STATE_RECONNECTING:
            snprintf(buf, size, "CONNECTION LOST. RECONNECTING...");
            break;
        case NET_STATE_ERROR:
            snprintf(buf, size, "%s", network_error_message());
            break;
    }
}

void online_lobby_draw(u8 alpha) {
    static const u8 textOnlineHud[] = { 0x18, 0x17, 0x15, 0x12, 0x17, 0x0E, 0xFF };  // "ONLINE"
    char tmp[64];
    s32 i, row;
    u8 dim = (u8) (alpha * 2 / 3);

    // Title in the big HUD font.
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, alpha);
    print_hud_lut_string(2, 116, 20, textOnlineHud);
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_end);

    draw_selection_highlight(alpha);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);

    // Labels: dimmed when they can no longer be edited.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 140,
                   settings_editable() ? MIN(alpha, 200) : dim);
    print_ascii(LOBBY_LEFT_X, field_row_y(FIELD_NAME), "NAME");
    print_ascii(LOBBY_LEFT_X, field_row_y(FIELD_SERVER), "SERVER");
    print_ascii(LOBBY_LEFT_X, field_row_y(FIELD_ROOM), "ROOM");
    print_ascii(LOBBY_LEFT_X, field_row_y(FIELD_COLOR), "COLOR");
    print_ascii(LOBBY_LEFT_X, field_row_y(FIELD_TYPE), "TYPE");

    // Values.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255,
                   settings_editable() ? MIN(alpha, 220) : dim);
    {
        // A blinking dash marks the field being typed into.
        const char *cursor = ((gGlobalTimer >> 3) & 1) ? "-" : "";
        snprintf(tmp, sizeof(tmp), "%s%s", configNetName,
                 (text_input_active() && sSel == FIELD_NAME) ? cursor : "");
        print_ascii(LOBBY_VALUE_X, field_row_y(FIELD_NAME), tmp);
        snprintf(tmp, sizeof(tmp), "%.28s%s", configNetServer,
                 (text_input_active() && sSel == FIELD_SERVER) ? cursor : "");
        print_ascii(LOBBY_VALUE_X, field_row_y(FIELD_SERVER), tmp);
        snprintf(tmp, sizeof(tmp), "%s%s", configNetRoom,
                 (text_input_active() && sSel == FIELD_ROOM) ? cursor : "");
        print_ascii(LOBBY_VALUE_X, field_row_y(FIELD_ROOM), tmp);
    }
    print_ascii(LOBBY_VALUE_X, field_row_y(FIELD_TYPE), sPublic ? "PUBLIC" : "PRIVATE");

    // Read-only: the mode comes from the (room creator's) OPTION screen.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 140, dim);
    print_ascii(LOBBY_LEFT_X, LOBBY_MODE_ROW_Y, "MODE");
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, dim);
    snprintf(tmp, sizeof(tmp), "%s - SET IN OPTION", mode_name());
    print_ascii(LOBBY_VALUE_X, LOBBY_MODE_ROW_Y, tmp);

    gDPSetEnvColor(gDisplayListHead++, sColorRGB[configNetColor][0],
                   sColorRGB[configNetColor][1], sColorRGB[configNetColor][2],
                   MIN(alpha, 220));
    print_ascii(LOBBY_VALUE_X, field_row_y(FIELD_COLOR), sColorNames[configNetColor]);

    // Action rows.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, MIN(alpha, 220));
    print_ascii(LOBBY_LEFT_X, field_row_y(FIELD_CONNECT), connect_label());
    if (network_state() == NET_STATE_LOBBY) {
        print_ascii(LOBBY_LEFT_X, field_row_y(FIELD_READY),
                    network_local_ready() ? "UNREADY" : "READY UP");
    } else {
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, dim);
        print_ascii(LOBBY_LEFT_X, field_row_y(FIELD_READY), "READY UP");
    }

    // Roster.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 140, MIN(alpha, 200));
    print_ascii(LOBBY_RIGHT_X, LOBBY_TOP_Y, "PLAYERS");
    row = 1;
    for (i = 0; i < NET_MAX_PLAYERS && row <= 8; i++) {
        struct NetPlayer *p = &gNetPlayers[i];
        if (!p->active) {
            continue;
        }
        gDPSetEnvColor(gDisplayListHead++, sColorRGB[p->color][0],
                       sColorRGB[p->color][1], sColorRGB[p->color][2],
                       p->connected ? MIN(alpha, 220) : dim);
        print_ascii(LOBBY_RIGHT_X, LOBBY_TOP_Y - row * LOBBY_ROW_H, p->name);
        if (!p->connected) {
            gDPSetEnvColor(gDisplayListHead++, 255, 120, 120, MIN(alpha, 220));
            print_ascii(LOBBY_RIGHT_X + 90, LOBBY_TOP_Y - row * LOBBY_ROW_H, "-");
        } else if (p->ready || network_state() == NET_STATE_COUNTDOWN
            || network_state() == NET_STATE_RACING) {
            gDPSetEnvColor(gDisplayListHead++, 110, 255, 110, MIN(alpha, 220));
            print_ascii(LOBBY_RIGHT_X + 90, LOBBY_TOP_Y - row * LOBBY_ROW_H, "OK");
        }
        row++;
    }

    // Status line, plus a typing hint while a field has the keyboard.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, MIN(alpha, 200));
    if (text_input_active()) {
        print_ascii(LOBBY_LEFT_X, 22, "TYPE ON KEYBOARD. ENTER WHEN DONE");
    } else {
        status_text(tmp, sizeof(tmp));
        print_ascii(LOBBY_LEFT_X, 22, tmp);
    }

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

#endif // TARGET_N64
