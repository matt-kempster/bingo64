#ifndef ONLINE_LOBBY_H
#define ONLINE_LOBBY_H

#include <PR/ultratypes.h>

// The ONLINE lobby screen inside file select (PC only; on N64 these are
// never called). Room settings are edited here, the connection is made
// here, and the ready-up/countdown plays out here. See
// src/pc/network/network.h for the underlying client.

// Per-frame input. Returns 1 when the player wants to leave the screen
// (the connection, if any, stays up so the race can start from the main
// file-select screen).
s32 online_lobby_handle_input(void);

// Draw the lobby. `alpha` is file select's fade-in text alpha.
void online_lobby_draw(u8 alpha);

#endif // ONLINE_LOBBY_H
