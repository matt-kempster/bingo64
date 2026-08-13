#ifndef TEXT_INPUT_H
#define TEXT_INPUT_H

// Line-edit mode for menu text fields (player name, server address, ...).
// While active, typed characters go into the caller's buffer and the
// keyboard stops acting as a game controller (a gamepad still works).
// The window backends feed characters in: SDL via SDL_TEXTINPUT, the
// Windows DXGI backend via WM_CHAR.

#ifdef __cplusplus
extern "C" {
#endif

// Begin editing `buf` (a NUL-terminated string, edited in place, at most
// maxLen bytes including the terminator). The existing text is kept, with
// the cursor at the end.
void text_input_start(char *buf, int maxLen);
void text_input_stop(void);
int text_input_active(void);

// One-shot: did the user finish editing (enter) or cancel (escape, which
// also restores the original text) since the last call?
int text_input_take_finished(void);

// One-shot: was ENTER pressed while NOT typing? Menus use this as the
// "activate/edit the selected thing" key; it is not a bindable N64 button.
int text_input_take_enter_key(void);

// Backends push characters here. Printable ASCII is appended; 0x08 is
// backspace, 0x0D/0x0A finishes, 0x1B cancels. Spaces are dropped: every
// text field feeds the space-separated relay protocol.
void text_input_on_char(int c);

#ifdef __cplusplus
}
#endif

#endif // TEXT_INPUT_H
