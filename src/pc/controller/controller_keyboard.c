#include <stdbool.h>
#include <ultra64.h>

#include "controller_api.h"

#ifdef TARGET_WEB
#include "controller_emscripten_keyboard.h"
#endif

#include <stdio.h>
#include <string.h>

#include "../configfile.h"
#include "controller_keyboard.h"
#include "text_input.h"
#ifdef TOUCH_CONTROLS
#include "controller_touchscreen.h"
#endif

static int keyboard_buttons_down;

// --- menu text-field editing (see text_input.h) ---

static char *sTextBuf = NULL;
static int sTextMax = 0;
static char sTextOriginal[128];
static int sTextFinished = 0;
// ENTER pressed while NOT typing: menus use it as the "activate/edit" key
// (it is not an N64 button, so it never collides with gameplay binds).
static int sMenuEnterPressed = 0;

int text_input_active(void) {
    return sTextBuf != NULL;
}

void text_input_start(char *buf, int maxLen) {
    sTextBuf = buf;
    sTextMax = maxLen;
    snprintf(sTextOriginal, sizeof(sTextOriginal), "%s", buf);
    sTextFinished = 0;
    // Whatever keys were held for gameplay must not stick while typing.
    keyboard_on_all_keys_up();
}

void text_input_stop(void) {
    sTextBuf = NULL;
}

int text_input_take_finished(void) {
    int f = sTextFinished;
    sTextFinished = 0;
    return f;
}

int text_input_take_enter_key(void) {
    int p = sMenuEnterPressed;
    sMenuEnterPressed = 0;
    return p;
}

void text_input_on_char(int c) {
    int len;
    if (sTextBuf == NULL) {
        return;
    }
    len = (int) strlen(sTextBuf);
    if (c == '\b') {
        if (len > 0) {
            sTextBuf[len - 1] = '\0';
        }
    } else if (c == '\r' || c == '\n') {
        sTextFinished = 1;
    } else if (c == 0x1B) {
        snprintf(sTextBuf, sTextMax, "%s", sTextOriginal);
        sTextFinished = 1;
    } else if (c > ' ' && c < 0x7F && len < sTextMax - 1) {
        // Printable ASCII except space: these strings travel in the
        // space-separated relay protocol.
        sTextBuf[len] = (char) c;
        sTextBuf[len + 1] = '\0';
    }
}

#define MAX_KEYBINDS 64
static int keyboard_mapping[MAX_KEYBINDS][2];
static int num_keybinds = 0;

static u32 keyboard_lastkey = VK_INVALID;

static int keyboard_map_scancode(int scancode) {
    int ret = 0;
    for (int i = 0; i < num_keybinds; i++) {
        if (keyboard_mapping[i][0] == scancode) {
            ret |= keyboard_mapping[i][1];
        }
    }
    return ret;
}

#define SCANCODE_M 0x32

bool keyboard_on_key_down(int scancode) {
    int mapped;
    if (text_input_active()) {
        // The keyboard is a text field right now, not a controller.
        return false;
    }
    if (scancode == 0x1C || scancode == 0x11C) {  // ENTER / numpad ENTER
        sMenuEnterPressed = 1;
    }
    mapped = keyboard_map_scancode(scancode);
    // M toggles this window's audio mute, unless the user bound M to a button.
    if (scancode == SCANCODE_M && mapped == 0) {
        extern unsigned char gAudioMuted;
        gAudioMuted = !gAudioMuted;
    }
    keyboard_buttons_down |= mapped;
    keyboard_lastkey = scancode;
    return mapped != 0;
}

bool keyboard_on_key_up(int scancode) {
    int mapped = keyboard_map_scancode(scancode);
    keyboard_buttons_down &= ~mapped;
    if (keyboard_lastkey == (u32) scancode)
        keyboard_lastkey = VK_INVALID;
    return mapped != 0;
}

void keyboard_on_all_keys_up(void) {
    keyboard_buttons_down = 0;
}

static void keyboard_add_binds(int mask, unsigned int *scancode) {
    for (int i = 0; i < MAX_BINDS && num_keybinds < MAX_KEYBINDS; ++i) {
        if (scancode[i] < VK_BASE_KEYBOARD + VK_SIZE) {
            keyboard_mapping[num_keybinds][0] = scancode[i];
            keyboard_mapping[num_keybinds][1] = mask;
            num_keybinds++;
        }
    }
}

static void keyboard_bindkeys(void) {
    bzero(keyboard_mapping, sizeof(keyboard_mapping));
    num_keybinds = 0;

    keyboard_add_binds(STICK_UP,     configKeyStickUp);
    keyboard_add_binds(STICK_LEFT,   configKeyStickLeft);
    keyboard_add_binds(STICK_DOWN,   configKeyStickDown);
    keyboard_add_binds(STICK_RIGHT,  configKeyStickRight);
    keyboard_add_binds(A_BUTTON,     configKeyA);
    keyboard_add_binds(B_BUTTON,     configKeyB);
    keyboard_add_binds(Z_TRIG,       configKeyZ);
    keyboard_add_binds(U_CBUTTONS,   configKeyCUp);
    keyboard_add_binds(L_CBUTTONS,   configKeyCLeft);
    keyboard_add_binds(D_CBUTTONS,   configKeyCDown);
    keyboard_add_binds(R_CBUTTONS,   configKeyCRight);
    keyboard_add_binds(U_JPAD,       configKeyDUp);
    keyboard_add_binds(L_JPAD,       configKeyDLeft);
    keyboard_add_binds(D_JPAD,       configKeyDDown);
    keyboard_add_binds(R_JPAD,       configKeyDRight);
    keyboard_add_binds(L_TRIG,       configKeyL);
    keyboard_add_binds(R_TRIG,       configKeyR);
    keyboard_add_binds(START_BUTTON, configKeyStart);
}

static void keyboard_init(void) {
    keyboard_bindkeys();

#ifdef TARGET_WEB
    controller_emscripten_keyboard_init();
#endif
}

static void keyboard_read(OSContPad *pad) {
    pad->button |= keyboard_buttons_down;
#ifdef TOUCH_CONTROLS
    if (keyboard_buttons_down) gTouchControlsInUse = FALSE;
#endif
    const u32 xstick = keyboard_buttons_down & STICK_XMASK;
    const u32 ystick = keyboard_buttons_down & STICK_YMASK;
    if (xstick == STICK_LEFT)
        pad->stick_x = -128;
    else if (xstick == STICK_RIGHT)
        pad->stick_x = 127;
    if (ystick == STICK_DOWN)
        pad->stick_y = -128;
    else if (ystick == STICK_UP)
        pad->stick_y = 127;
}

static u32 keyboard_rawkey(void) {
    const u32 ret = keyboard_lastkey;
    keyboard_lastkey = VK_INVALID;
    return ret;
}

static void keyboard_shutdown(void) {
}

struct ControllerAPI controller_keyboard = {
    VK_BASE_KEYBOARD,
    keyboard_init,
    keyboard_read,
    keyboard_rawkey,
    NULL,
    NULL,
    keyboard_bindkeys,
    keyboard_shutdown
};
