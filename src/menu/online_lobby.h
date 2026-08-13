#ifndef ONLINE_LOBBY_H
#define ONLINE_LOBBY_H

#include <PR/ultratypes.h>

// The ONLINE lobby screen inside file select (PC only; on N64 these are
// never called). The lobby is fully cursor-driven: click a field row to
// edit it, click one of the four 3D action buttons (spawned by
// file_select.c) to act. Room settings belong to the host and freeze once
// the race starts. See src/pc/network/network.h for the underlying client.

// The four action buttons along the bottom of the lobby, left to right.
enum LobbyButton {
    LOBBY_BTN_CONNECT,
    LOBBY_BTN_READY,
    LOBBY_BTN_OPTIONS,
    LOBBY_BTN_START,
    LOBBY_BTN_COUNT
};

// Per-frame input. curX/curY is the cursor in 320x240 screen units.
// Returns 1 when the player wants to leave the screen (the connection, if
// any, stays up), -1 when A was pressed away from any field row (the
// caller turns that into a click for the 3D buttons), 0 otherwise.
s32 online_lobby_handle_input(f32 curX, f32 curY);

// Draw the lobby. `alpha` is file select's fade-in text alpha; curX/curY
// as above (drives the hover highlight).
void online_lobby_draw(u8 alpha, f32 curX, f32 curY);

// World-space offsets (relative to the fullscreen lobby backdrop, click
// depth 22) where file_select.c spawns each action button.
void online_lobby_button_world_pos(s32 which, s16 *x, s16 *y);

// May this button be pressed right now? Inactive buttons render recessed
// and clicking them buzzes.
s32 online_lobby_button_active(s32 which);

// A click landed on an active button. Returns 2 when the OPTIONS screen
// should open (the caller owns that transition), 0 otherwise.
s32 online_lobby_button_pressed(s32 which);

// ASCII -> menu-font printing, shared with file_select's PC-only screens.
void net_print_ascii(s16 x, s16 y, const char *str);
// Center on x using the generic font's kerning table.
void net_print_ascii_centered(s16 centerX, s16 y, const char *str);

#endif // ONLINE_LOBBY_H
