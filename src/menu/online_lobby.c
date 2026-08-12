// The ONLINE lobby screen inside file select (PC only).
// Fully cursor-driven: click a field row to edit it (text fields grab the
// keyboard via text_input.h), click the 3D action buttons to act. Room
// settings (seed/mode/objectives) belong to the host and freeze once the
// race starts.

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
#include "gfx_dimensions.h"
#include "sm64.h"
#include "sounds.h"

#include "pc/configfile.h"
#include "pc/controller/text_input.h"
#include "pc/network/network.h"

// Editable field rows (top-down). The action buttons are separate 3D
// objects owned by file_select.c.
enum LobbyField {
    FIELD_NAME,
    FIELD_COLOR,
    FIELD_SERVER,
    FIELD_ROOM,
    FIELD_TYPE,
    FIELD_SEED,     // room setting: the host's seed proposal
    FIELD_MODE,     // room setting: read-only here, set on the OPTIONS screen
    FIELD_COUNT
};

static s32 sPublic = 0;
static s32 sEditingField = -1;
static char sSeedBuf[16];

// Seed helpers from file_select.c (the same value the N64 numpad edits).
extern void bingo_seed_set_from_ascii(const char *str);
extern void bingo_seed_to_ascii(char *out, s32 size);

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

void net_print_ascii(s16 x, s16 y, const char *str) {
    u8 buf[64];
    ascii_to_menu(buf, str, sizeof(buf));
    print_generic_string(x, y, buf);
}

#define print_ascii net_print_ascii

// ---------------------------------------------------------------------------
// Layout. The fullscreen backdrop's frame bevels are pinned to the window
// edges (file_select_fit_screen), so the field column anchors to the LEFT
// edge and the roster to the RIGHT edge with the same macros; centered
// things (title, buttons, status) anchor to x=160, which every aspect
// ratio agrees on.

#define LOBBY_EDGE_PAD_L 16   // tracks the backdrop frame's left bevel
#define LOBBY_EDGE_PAD_R 130  // roster column, from the right edge
#define LOBBY_TOP_Y      168
#define LOBBY_ROW_H      14
#define LOBBY_HEADER_Y   96   // "ROOM SETTINGS" section header
#define LOBBY_STATUS_Y   182  // status line, up top under the title
#define LOBBY_LABEL_Y    26   // labels under the action buttons

static s16 lobby_left_x(void) {
    return (s16) GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(LOBBY_EDGE_PAD_L);
}

static s16 lobby_roster_x(void) {
    return (s16) GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(LOBBY_EDGE_PAD_R);
}

static s32 field_row_y(s32 field) {
    switch (field) {
        case FIELD_SEED: return LOBBY_HEADER_Y - LOBBY_ROW_H;
        case FIELD_MODE: return LOBBY_HEADER_Y - 2 * LOBBY_ROW_H;
        default:         return LOBBY_TOP_Y - field * LOBBY_ROW_H;
    }
}

// The four action buttons, in world units relative to the fullscreen
// backdrop (click depth 22; world/7.208 = screen units from center).
static const s16 sBtnWorldX[LOBBY_BTN_COUNT] = { -675, -225, 225, 675 };
#define LOBBY_BTN_WORLD_Y (-548)

void online_lobby_button_world_pos(s32 which, s16 *x, s16 *y) {
    // The fullscreen parent is yaw-rotated 180, so child X mirrors at
    // render time: negate so the button lands at sBtnWorldX on screen.
    *x = -sBtnWorldX[which];
    *y = LOBBY_BTN_WORLD_Y;
}

static s16 button_unit_x(s32 which) {
    return (s16) (160 + sBtnWorldX[which] / 7.208f);
}

// ---------------------------------------------------------------------------
// State predicates.

// May we still edit connection settings? Only before connecting.
static s32 settings_editable(void) {
    enum NetState st = network_state();
    return st == NET_STATE_OFF || st == NET_STATE_ERROR;
}

// The seed is editable offline, and by the host until the race starts.
static s32 seed_editable(void) {
    return settings_editable()
           || (network_is_host() && !network_room_locked());
}

static s32 field_editable(s32 field) {
    switch (field) {
        case FIELD_SEED: return seed_editable();
        case FIELD_MODE: return 0;  // set on the OPTIONS screen
        default:         return settings_editable();
    }
}

// Which editable field row is under the cursor (screen units)?
static s32 field_at(f32 x, f32 y) {
    s32 f;
    s16 leftX = lobby_left_x();
    if (x < leftX - 6 || x > leftX + 150) {
        return -1;
    }
    for (f = 0; f < FIELD_COUNT; f++) {
        s32 ry = field_row_y(f);
        if (y >= ry - 6 && y <= ry + 12) {
            return field_editable(f) ? f : -1;
        }
    }
    return -1;
}

s32 online_lobby_button_active(s32 which) {
    if (network_state() == NET_STATE_COUNTDOWN) {
        return 0;  // locked during the countdown; GO launches everyone
    }
    switch (which) {
        case LOBBY_BTN_CONNECT:
            return 1;
        case LOBBY_BTN_READY:
            return network_state() == NET_STATE_LOBBY;
        case LOBBY_BTN_OPTIONS:
            return 1;
        case LOBBY_BTN_START:
            return network_is_host() && network_state() == NET_STATE_LOBBY;
    }
    return 0;
}

s32 online_lobby_button_pressed(s32 which) {
    switch (which) {
        case LOBBY_BTN_CONNECT:
            if (settings_editable()) {
                network_connect(configNetServer, configNetRoom, configNetName,
                                (s32) configNetColor, sPublic);
            } else {
                network_disconnect();
            }
            break;
        case LOBBY_BTN_READY:
            network_set_ready(!network_local_ready());
            break;
        case LOBBY_BTN_OPTIONS:
            return 2;  // file_select opens the OPTIONS screen over us
        case LOBBY_BTN_START:
            network_start_race();
            break;
    }
    return 0;
}

static void activate_field(s32 field) {
    switch (field) {
        case FIELD_NAME:
            sEditingField = field;
            text_input_start(configNetName, sizeof(configNetName));
            break;
        case FIELD_SERVER:
            sEditingField = field;
            text_input_start(configNetServer, sizeof(configNetServer));
            break;
        case FIELD_ROOM:
            sEditingField = field;
            text_input_start(configNetRoom, sizeof(configNetRoom));
            break;
        case FIELD_COLOR:
            configNetColor = (configNetColor + 1) % NET_COLOR_COUNT;
            break;
        case FIELD_TYPE:
            sPublic ^= 1;
            break;
        case FIELD_SEED:
            sEditingField = field;
            bingo_seed_to_ascii(sSeedBuf, sizeof(sSeedBuf));
            text_input_start(sSeedBuf, 10);  // 9 digits + terminator
            break;
    }
    play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
}

s32 online_lobby_handle_input(f32 curX, f32 curY) {
    u16 pressed;

    if (text_input_active()) {
        if (text_input_take_finished()) {
            text_input_stop();
            if (sEditingField == FIELD_SEED) {
                bingo_seed_set_from_ascii(sSeedBuf);
                network_push_local_options();
            }
            sEditingField = -1;
        }
        return 0;
    }

    if (network_state() == NET_STATE_COUNTDOWN) {
        return 0;  // everything is locked; GO is coming
    }

    pressed = gPlayer3Controller->buttonPressed;
    if (pressed & (B_BUTTON | START_BUTTON)) {
        return 1;
    }
    if (pressed & A_BUTTON) {
        s32 f = field_at(curX, curY);
        if (f >= 0) {
            activate_field(f);
        } else {
            return -1;  // maybe one of the 3D buttons; file_select checks
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Drawing.

// The room's game mode: the host sets it on the OPTIONS screen and the
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

static void draw_hover_highlight(u8 alpha, f32 curX, f32 curY) {
    s32 f = field_at(curX, curY);
    s32 y;
    if (f < 0) {
        return;
    }
    y = field_row_y(f);
    // Fill rects use top-down screen coords; text y counts from the bottom.
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF);
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 38, 38, 38, MIN(alpha, 150));
    gDPFillRectangle(gDisplayListHead++, lobby_left_x() - 3, 240 - y - 13,
                     lobby_left_x() + 148, 240 - y + 2);
}

static const char *button_label(s32 which) {
    switch (which) {
        case LOBBY_BTN_CONNECT:
            switch (network_state()) {
                case NET_STATE_OFF:
                case NET_STATE_ERROR:
                    return "CONNECT";
                case NET_STATE_CONNECTING:
                    return "CANCEL";
                default:
                    return "LEAVE";
            }
        case LOBBY_BTN_READY:
            return network_local_ready() ? "UNREADY" : "READY UP";
        case LOBBY_BTN_OPTIONS:
            return "OPTIONS";
        case LOBBY_BTN_START:
            if (network_state() == NET_STATE_COUNTDOWN
                || network_state() == NET_STATE_RACING) {
                return "STARTED";
            }
            if (network_active() && !network_is_host()) {
                return "HOST STARTS";
            }
            return "START RACE";
    }
    return "";
}

static s32 all_connected_ready(void) {
    s32 i;
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        if (gNetPlayers[i].active && gNetPlayers[i].connected
            && !gNetPlayers[i].ready) {
            return 0;
        }
    }
    return 1;
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
            snprintf(buf, size, "STARTING IN %d",
                     (network_countdown_frames() + 29) / 30);
            break;
        case NET_STATE_RACING:
            snprintf(buf, size, "GO");
            break;
        case NET_STATE_RECONNECTING:
            snprintf(buf, size, "CONNECTION LOST. RECONNECTING...");
            break;
        case NET_STATE_ERROR:
            snprintf(buf, size, "%s", network_error_message());
            break;
    }
}

void online_lobby_draw(u8 alpha, f32 curX, f32 curY) {
    static const u8 textOnlineHud[] = { 0x18, 0x17, 0x15, 0x12, 0x17, 0x0E, 0xFF };  // "ONLINE"
    char tmp[64];
    s32 i, row, b;
    u8 dim = (u8) (alpha * 2 / 3);
    s16 leftX = lobby_left_x();
    s16 valueX = leftX + 58;
    s16 rosterX = lobby_roster_x();

    // Title in the big HUD font.
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, alpha);
    print_hud_lut_string(2, 116, 20, textOnlineHud);
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_end);

    draw_hover_highlight(alpha, curX, curY);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);

    // Field labels: dimmed when they can no longer be edited.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 140,
                   settings_editable() ? MIN(alpha, 200) : dim);
    print_ascii(leftX, field_row_y(FIELD_NAME), "NAME");
    print_ascii(leftX, field_row_y(FIELD_COLOR), "COLOR");
    print_ascii(leftX, field_row_y(FIELD_SERVER), "SERVER");
    print_ascii(leftX, field_row_y(FIELD_ROOM), "ROOM");
    print_ascii(leftX, field_row_y(FIELD_TYPE), "TYPE");

    // The room-settings section: the host's controls, frozen at start.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 140, MIN(alpha, 200));
    if (network_room_locked()) {
        print_ascii(leftX, LOBBY_HEADER_Y, "ROOM SETTINGS - LOCKED");
    } else if (network_active() && !network_is_host()) {
        print_ascii(leftX, LOBBY_HEADER_Y, "ROOM SETTINGS - HOST'S");
    } else {
        print_ascii(leftX, LOBBY_HEADER_Y, "ROOM SETTINGS");
    }
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 140,
                   seed_editable() ? MIN(alpha, 200) : dim);
    print_ascii(leftX, field_row_y(FIELD_SEED), "SEED");
    print_ascii(leftX, field_row_y(FIELD_MODE), "MODE");

    // Values.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255,
                   settings_editable() ? MIN(alpha, 220) : dim);
    {
        // A blinking dash marks the field being typed into.
        const char *cursor = ((gGlobalTimer >> 3) & 1) ? "-" : "";
        snprintf(tmp, sizeof(tmp), "%s%s", configNetName,
                 (text_input_active() && sEditingField == FIELD_NAME) ? cursor : "");
        print_ascii(valueX, field_row_y(FIELD_NAME), tmp);
        snprintf(tmp, sizeof(tmp), "%.28s%s", configNetServer,
                 (text_input_active() && sEditingField == FIELD_SERVER) ? cursor : "");
        print_ascii(valueX, field_row_y(FIELD_SERVER), tmp);
        snprintf(tmp, sizeof(tmp), "%s%s", configNetRoom,
                 (text_input_active() && sEditingField == FIELD_ROOM) ? cursor : "");
        print_ascii(valueX, field_row_y(FIELD_ROOM), tmp);
    }
    print_ascii(valueX, field_row_y(FIELD_TYPE), sPublic ? "PUBLIC" : "PRIVATE");

    gDPSetEnvColor(gDisplayListHead++, sColorRGB[configNetColor][0],
                   sColorRGB[configNetColor][1], sColorRGB[configNetColor][2],
                   MIN(alpha, 220));
    print_ascii(valueX, field_row_y(FIELD_COLOR), sColorNames[configNetColor]);

    // The seed: the host's proposal; hidden from peers so nobody can
    // preview the board.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255,
                   seed_editable() ? MIN(alpha, 220) : dim);
    if (network_active() && !network_is_host()) {
        print_ascii(valueX, field_row_y(FIELD_SEED), "HOST'S CHOICE");
    } else if (text_input_active() && sEditingField == FIELD_SEED) {
        const char *cursor = ((gGlobalTimer >> 3) & 1) ? "-" : "";
        snprintf(tmp, sizeof(tmp), "%s%s", sSeedBuf, cursor);
        print_ascii(valueX, field_row_y(FIELD_SEED), tmp);
    } else {
        bingo_seed_to_ascii(tmp, sizeof(tmp));
        print_ascii(valueX, field_row_y(FIELD_SEED),
                    tmp[0] != '\0' ? tmp : "RANDOM");
    }
    print_ascii(valueX, field_row_y(FIELD_MODE), mode_name());

    // Labels under the four action buttons; recessed buttons get dim text.
    for (b = 0; b < LOBBY_BTN_COUNT; b++) {
        const char *label = button_label(b);
        s32 active = online_lobby_button_active(b);
        if (b == LOBBY_BTN_START && active && all_connected_ready()) {
            gDPSetEnvColor(gDisplayListHead++, 110, 255, 110, MIN(alpha, 220));
        } else {
            gDPSetEnvColor(gDisplayListHead++, 255, 255, 255,
                           active ? MIN(alpha, 220) : dim);
        }
        print_ascii(button_unit_x(b) - (s32) strlen(label) * 15 / 4,
                    LOBBY_LABEL_Y, label);
    }

    // Roster: name in the player's color, a marker column on the right.
    // The host wears the gold HOST tag.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 140, MIN(alpha, 200));
    print_ascii(rosterX, LOBBY_TOP_Y, "PLAYERS");
    row = 1;
    for (i = 0; i < NET_MAX_PLAYERS && row <= 8; i++) {
        struct NetPlayer *p = &gNetPlayers[i];
        s32 rowY;
        if (!p->active) {
            continue;
        }
        rowY = LOBBY_TOP_Y - row * LOBBY_ROW_H;
        gDPSetEnvColor(gDisplayListHead++, sColorRGB[p->color][0],
                       sColorRGB[p->color][1], sColorRGB[p->color][2],
                       p->connected ? MIN(alpha, 220) : dim);
        print_ascii(rosterX, rowY, p->name);
        if (network_active() && p->id == network_host_id()) {
            gDPSetEnvColor(gDisplayListHead++, 255, 220, 90, MIN(alpha, 220));
            print_ascii(rosterX + 80, rowY, "HOST");
        } else if (!p->connected) {
            gDPSetEnvColor(gDisplayListHead++, 255, 120, 120, MIN(alpha, 220));
            print_ascii(rosterX + 80, rowY, "-");
        } else if (p->ready || network_state() == NET_STATE_COUNTDOWN
            || network_state() == NET_STATE_RACING) {
            gDPSetEnvColor(gDisplayListHead++, 110, 255, 110, MIN(alpha, 220));
            print_ascii(rosterX + 80, rowY, "OK");
        }
        row++;
    }

    // Status line under the title, plus a typing hint while a field has
    // the keyboard.
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, MIN(alpha, 200));
    if (text_input_active()) {
        print_ascii(leftX, LOBBY_STATUS_Y, "TYPE ON KEYBOARD. ENTER WHEN DONE");
    } else {
        status_text(tmp, sizeof(tmp));
        print_ascii(leftX, LOBBY_STATUS_Y, tmp);
    }

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

#endif // TARGET_N64
