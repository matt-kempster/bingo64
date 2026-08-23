#ifdef CAPI_SDL2

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <SDL2/SDL.h>

// Analog camera movement by Pathétique (github.com/vrmiguel), y0shin and Mors
// Contribute or communicate bugs at github.com/vrmiguel/sm64-analog-camera

#include <ultra64.h>

#include "controller_api.h"
#include "controller_sdl.h"
#include "../configfile.h"
#include "../platform.h"
#include "../fs/fs.h"
#ifdef TOUCH_CONTROLS
#include "controller_touchscreen.h"
#endif

#ifdef MOUSE_ACTIONS
#include "controller_mouse.h"
#endif

#include "game/level_update.h"

// mouse buttons are also in the controller namespace (why), just offset 0x100
#define VK_OFS_SDL_MOUSE 0x0100
#define VK_BASE_SDL_MOUSE (VK_BASE_SDL_GAMEPAD + VK_OFS_SDL_MOUSE)
#define MAX_JOYBINDS 32
#define MAX_MOUSEBUTTONS 8 // arbitrary
#define MAX_JOYBUTTONS 64  // arbitrary; includes virtual keys for triggers and stick directions
#define AXIS_THRESHOLD (30 * 256)
// stick directions need a firmer tilt than the triggers to count as a press,
// matching the old hardcoded right-stick-as-C threshold
#define AXIS_DIGITAL_THRESHOLD 0x4000

#define VK_OFS_STICK_DIR (VK_LSTICK_UP - VK_BASE_SDL_GAMEPAD)
#define STICK_DIR_COUNT 8

static bool init_ok;
static bool haptics_enabled;
static SDL_GameController *sdl_cntrl;
// raw fallback for pads SDL has no gamepad mapping for (e.g. some 8BitDo
// models); buttons, hat and axes go through the same virtual-key bind system
static SDL_Joystick *sdl_joy;
static SDL_Haptic *sdl_haptic;

static u32 joy_binds[MAX_JOYBINDS][2];
static u32 num_joy_binds = 0;
static bool joy_buttons[MAX_JOYBUTTONS] = { false };
static u32 last_joybutton = VK_INVALID;

// A stick with any of its direction keys bound in the config is "remapped":
// it loses its legacy hardcoded role (left = movement, right = C-buttons).
static bool l_stick_rebound = false;
static bool r_stick_rebound = false;
static s32 ext_stick_source = 1; // stick feeding ext_stick: 0 left, 1 right, -1 none

#ifdef MOUSE_ACTIONS
static u32 mouse_binds[MAX_JOYBINDS][2];
static u32 num_mouse_binds = 0;
static u32 last_mouse = VK_INVALID;
#endif

static inline void controller_add_binds(const u32 mask, const u32 *btns) {
    for (u32 i = 0; i < MAX_BINDS; ++i) {
        // mouse VKs live inside the gamepad VK_SIZE range, so the upper bound
        // must stop at VK_BASE_SDL_MOUSE or mouse binds corrupt joy_binds
        if (btns[i] >= VK_BASE_SDL_GAMEPAD && btns[i] < VK_BASE_SDL_MOUSE
            && num_joy_binds < MAX_JOYBINDS) {
            joy_binds[num_joy_binds][0] = btns[i] - VK_BASE_SDL_GAMEPAD;
            joy_binds[num_joy_binds][1] = mask;
            ++num_joy_binds;
        }
#ifdef MOUSE_ACTIONS
        if (btns[i] >= VK_BASE_SDL_MOUSE && btns[i] < VK_BASE_SDL_MOUSE + MAX_MOUSEBUTTONS
            && num_mouse_binds < MAX_JOYBINDS && configMouse) {
            mouse_binds[num_mouse_binds][0] = btns[i] - VK_BASE_SDL_MOUSE;
            mouse_binds[num_mouse_binds][1] = mask;
            ++num_mouse_binds;
        }
#endif
    }
}

static void controller_sdl_bind(void) {
    bzero(joy_binds, sizeof(joy_binds));
    num_joy_binds = 0;
#ifdef MOUSE_ACTIONS
    bzero(mouse_binds, sizeof(mouse_binds));
    num_mouse_binds = 0;
#endif

    controller_add_binds(A_BUTTON,     configKeyA);
    controller_add_binds(B_BUTTON,     configKeyB);
    controller_add_binds(Z_TRIG,       configKeyZ);
    controller_add_binds(STICK_UP,     configKeyStickUp);
    controller_add_binds(STICK_LEFT,   configKeyStickLeft);
    controller_add_binds(STICK_DOWN,   configKeyStickDown);
    controller_add_binds(STICK_RIGHT,  configKeyStickRight);
    controller_add_binds(U_CBUTTONS,   configKeyCUp);
    controller_add_binds(L_CBUTTONS,   configKeyCLeft);
    controller_add_binds(D_CBUTTONS,   configKeyCDown);
    controller_add_binds(R_CBUTTONS,   configKeyCRight);
    controller_add_binds(U_JPAD,       configKeyDUp);
    controller_add_binds(L_JPAD,       configKeyDLeft);
    controller_add_binds(D_JPAD,       configKeyDDown);
    controller_add_binds(R_JPAD,       configKeyDRight);
    controller_add_binds(L_TRIG,       configKeyL);
    controller_add_binds(R_TRIG,       configKeyR);
    controller_add_binds(START_BUTTON, configKeyStart);

    l_stick_rebound = r_stick_rebound = false;
    ext_stick_source = -1;
    for (u32 i = 0; i < num_joy_binds; ++i) {
        const u32 vk = joy_binds[i][0];
        if (vk < VK_OFS_STICK_DIR || vk >= VK_OFS_STICK_DIR + STICK_DIR_COUNT)
            continue;
        const bool left = vk < VK_OFS_STICK_DIR + 4;
        if (left) l_stick_rebound = true;
        else      r_stick_rebound = true;
        // the camera / ext stick follows whichever stick drives the C-buttons
        if (joy_binds[i][1] & (U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS))
            ext_stick_source = left ? 0 : 1;
    }
    if (!r_stick_rebound)
        ext_stick_source = 1; // legacy: an untouched right stick is the C-stick
}

static void controller_sdl_init(void) {
    // try loading an external gamecontroller mapping file
    uint64_t gcsize = 0;
    void *gcdata = fs_load_file("gamecontrollerdb.txt", &gcsize);
    if (gcdata && gcsize) {
        SDL_RWops *rw = SDL_RWFromConstMem(gcdata, gcsize);
        if (rw) {
            int nummaps = SDL_GameControllerAddMappingsFromRW(rw, SDL_TRUE);
            if (nummaps >= 0)
                printf("loaded %d controller mappings from 'gamecontrollerdb.txt'\n", nummaps);
        }
        free(gcdata);
    }

    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL init error: %s\n", SDL_GetError());
        return;
    }

    haptics_enabled = (SDL_InitSubSystem(SDL_INIT_HAPTIC) == 0);

#ifdef MOUSE_ACTIONS
    if (mouse_has_center_control && sCurrPlayMode != 2) {
        controller_mouse_enter_relative();
    }
    controller_mouse_leave_relative();
#endif

    controller_sdl_bind();

    init_ok = true;
#ifdef MOUSE_ACTIONS
    mouse_init_ok = true;
#endif
}

static SDL_Haptic *controller_sdl_init_haptics(const int joy) {
    if (!haptics_enabled) return NULL;

    SDL_Haptic *hap = SDL_HapticOpen(joy);
    if (!hap) return NULL;

    if (SDL_HapticRumbleSupported(hap) != SDL_TRUE) {
        SDL_HapticClose(hap);
        return NULL;
    }

    if (SDL_HapticRumbleInit(hap) != 0) {
        SDL_HapticClose(hap);
        return NULL;
    }

    printf("controller %s has haptics support, rumble enabled\n", SDL_JoystickNameForIndex(joy));
    return hap;
}

static inline void update_button(const int i, const bool new) {
    const bool pressed = !joy_buttons[i] && new;
    joy_buttons[i] = new;
    if (pressed) last_joybutton = i;
}

#ifdef MOUSE_ACTIONS
static void mouse_control_handler(OSContPad *pad) {
    if (!configMouse) {
        return;
    }

    if (mouse_has_center_control && sCurrPlayMode != 2) {
        controller_mouse_enter_relative();
    } else {
        controller_mouse_leave_relative();
    }

    u32 mouse_prev = mouse_buttons;
    controller_mouse_read_relative();
    u32 mouse = mouse_buttons;

    for (u32 i = 0; i < num_mouse_binds; ++i)
        if (mouse & SDL_BUTTON(mouse_binds[i][0]))
            pad->button |= mouse_binds[i][1];

    // remember buttons that changed from 0 to 1
    last_mouse = (mouse_prev ^ mouse) & mouse;
}
#endif

static void controller_sdl_read(OSContPad *pad) {
    if (!init_ok) {
        return;
    }

#ifdef MOUSE_ACTIONS
    mouse_control_handler(pad);
#endif

    SDL_GameControllerUpdate();

    if (sdl_cntrl != NULL && !SDL_GameControllerGetAttached(sdl_cntrl)) {
        SDL_HapticClose(sdl_haptic);
        SDL_GameControllerClose(sdl_cntrl);
        sdl_cntrl = NULL;
        sdl_haptic = NULL;
    }
    if (sdl_joy != NULL && !SDL_JoystickGetAttached(sdl_joy)) {
        SDL_HapticClose(sdl_haptic);
        SDL_JoystickClose(sdl_joy);
        sdl_joy = NULL;
        sdl_haptic = NULL;
    }

    if (sdl_cntrl == NULL && sdl_joy == NULL) {
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) {
                sdl_cntrl = SDL_GameControllerOpen(i);
                if (sdl_cntrl != NULL) {
                    sdl_haptic = controller_sdl_init_haptics(i);
                    break;
                }
            }
        }
        // No mapped gamepad found: open the first joystick raw. Everything is
        // rebindable in the Options menu, and a gamecontrollerdb.txt in the
        // game folder can supply a proper mapping instead.
        if (sdl_cntrl == NULL) {
            for (int i = 0; i < SDL_NumJoysticks(); i++) {
                sdl_joy = SDL_JoystickOpen(i);
                if (sdl_joy != NULL) {
                    printf("controller '%s' has no gamepad mapping, using raw joystick mode\n",
                           SDL_JoystickNameForIndex(i));
                    sdl_haptic = controller_sdl_init_haptics(i);
                    break;
                }
            }
        }
        if (sdl_cntrl == NULL && sdl_joy == NULL) {
            return;
        }
    }

    int16_t leftx, lefty, rightx, righty, ltrig, rtrig;
    if (sdl_cntrl != NULL) {
        leftx = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_LEFTX);
        lefty = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_LEFTY);
        rightx = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_RIGHTX);
        righty = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_RIGHTY);

        ltrig = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        rtrig = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    } else {
        // raw mode: assume the common axis order, sticks first then triggers
        const int naxes = SDL_JoystickNumAxes(sdl_joy);
        leftx  = naxes > 0 ? SDL_JoystickGetAxis(sdl_joy, 0) : 0;
        lefty  = naxes > 1 ? SDL_JoystickGetAxis(sdl_joy, 1) : 0;
        rightx = naxes > 2 ? SDL_JoystickGetAxis(sdl_joy, 2) : 0;
        righty = naxes > 3 ? SDL_JoystickGetAxis(sdl_joy, 3) : 0;
        ltrig  = naxes > 4 ? SDL_JoystickGetAxis(sdl_joy, 4) : 0;
        rtrig  = naxes > 5 ? SDL_JoystickGetAxis(sdl_joy, 5) : 0;
    }

#ifdef TARGET_WEB
    // Firefox has a bug: https://bugzilla.mozilla.org/show_bug.cgi?id=1606562
    // It sets down y to 32768.0f / 32767.0f, which is greater than the allowed 1.0f,
    // which SDL then converts to a int16_t by multiplying by 32767.0f, which overflows into -32768.
    // Maximum up will hence never become -32768 with the current version of SDL2,
    // so this workaround should be safe in compliant browsers.
    if (lefty == -32768) {
        lefty = 32767;
    }
    if (righty == -32768) {
        righty = 32767;
    }
#endif

    if (sdl_cntrl != NULL) {
        for (u32 i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) {
            const bool new = SDL_GameControllerGetButton(sdl_cntrl, i);
#ifdef TOUCH_CONTROLS
            if (new) gTouchControlsInUse = FALSE;
#endif
            update_button(i, new);
        }
    } else {
        const int nbtns = SDL_JoystickNumButtons(sdl_joy);
        const bool hasHat = SDL_JoystickNumHats(sdl_joy) > 0;
        const int rawMax = VK_LTRIGGER - VK_BASE_SDL_GAMEPAD; // keep clear of the trigger/stick virtual keys
        for (int i = 0; i < nbtns && i < rawMax; ++i) {
            // the first hat owns the D-pad virtual keys below
            if (hasHat && i >= SDL_CONTROLLER_BUTTON_DPAD_UP && i <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
                continue;
            update_button(i, SDL_JoystickGetButton(sdl_joy, i));
        }
        // hat -> D-pad virtual keys, so the default D-pad binds work raw too
        if (hasHat) {
            const u8 hat = SDL_JoystickGetHat(sdl_joy, 0);
            update_button(SDL_CONTROLLER_BUTTON_DPAD_UP,    (hat & SDL_HAT_UP) != 0);
            update_button(SDL_CONTROLLER_BUTTON_DPAD_DOWN,  (hat & SDL_HAT_DOWN) != 0);
            update_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT,  (hat & SDL_HAT_LEFT) != 0);
            update_button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, (hat & SDL_HAT_RIGHT) != 0);
        }
    }

    update_button(VK_LTRIGGER - VK_BASE_SDL_GAMEPAD, ltrig > AXIS_THRESHOLD);
    update_button(VK_RTRIGGER - VK_BASE_SDL_GAMEPAD, rtrig > AXIS_THRESHOLD);

    // stick directions as virtual buttons, so the rebind menu can capture
    // stick motion and any control can be bound to a stick direction
    const s32 stick_dirs[STICK_DIR_COUNT] = {
        -lefty, lefty, -leftx, leftx,      // left stick: up, down, left, right
        -righty, righty, -rightx, rightx,  // right stick
    };
    for (u32 i = 0; i < STICK_DIR_COUNT; ++i)
        update_button(VK_OFS_STICK_DIR + i, stick_dirs[i] > AXIS_DIGITAL_THRESHOLD);

    u32 buttons_down = 0;
    for (u32 i = 0; i < num_joy_binds; ++i) {
        const u32 vk = joy_binds[i][0];
        // stick directions bound to the N64 stick keep their analog value:
        // routed below, not treated as digital presses
        if (vk >= VK_OFS_STICK_DIR && vk < VK_OFS_STICK_DIR + STICK_DIR_COUNT
            && (joy_binds[i][1] & (STICK_XMASK | STICK_YMASK)))
            continue;
        if (joy_buttons[vk])
            buttons_down |= joy_binds[i][1];
    }

    pad->button |= buttons_down;

    const u32 xstick = buttons_down & STICK_XMASK;
    const u32 ystick = buttons_down & STICK_YMASK;
    if (xstick == STICK_LEFT)
        pad->stick_x = -128;
    else if (xstick == STICK_RIGHT)
        pad->stick_x = 127;
    if (ystick == STICK_DOWN)
        pad->stick_y = -128;
    else if (ystick == STICK_UP)
        pad->stick_y = 127;

    const uint32_t stickDeadzoneActual = configStickDeadzone * DEADZONE_STEP;
    const uint32_t deadzone_sq = (uint32_t)(stickDeadzoneActual * stickDeadzoneActual);

    // legacy fixed roles apply only while a stick has no direction bound
    if (!r_stick_rebound) {
        if (rightx < -0x4000) pad->button |= L_CBUTTONS;
        if (rightx > 0x4000) pad->button |= R_CBUTTONS;
        if (righty < -0x4000) pad->button |= U_CBUTTONS;
        if (righty > 0x4000) pad->button |= D_CBUTTONS;
    }

    if (!l_stick_rebound) {
        uint32_t magnitude_sq = (uint32_t)(leftx * leftx) + (uint32_t)(lefty * lefty);
        if (magnitude_sq > deadzone_sq) {
            // Game expects stick coordinates within -80..80
            // 32768 / 409 = ~80
            pad->stick_x = leftx / 409;
            pad->stick_y = -lefty / 409;
        }
    }

    // stick directions bound to the N64 stick route their analog value
    if (l_stick_rebound || r_stick_rebound) {
        s32 sx = 0, sy = 0;
        for (u32 i = 0; i < num_joy_binds; ++i) {
            const u32 vk = joy_binds[i][0];
            if (vk < VK_OFS_STICK_DIR || vk >= VK_OFS_STICK_DIR + STICK_DIR_COUNT)
                continue;
            s32 v = stick_dirs[vk - VK_OFS_STICK_DIR];
            if (v < 0) v = 0;
            switch (joy_binds[i][1] & (STICK_XMASK | STICK_YMASK)) {
                case STICK_UP:    sy += v; break;
                case STICK_DOWN:  sy -= v; break;
                case STICK_LEFT:  sx -= v; break;
                case STICK_RIGHT: sx += v; break;
            }
        }
        // both sticks may feed one direction; keep the squares in s32 range
        if (sx > 32767) sx = 32767; else if (sx < -32767) sx = -32767;
        if (sy > 32767) sy = 32767; else if (sy < -32767) sy = -32767;
        if ((uint32_t)(sx * sx) + (uint32_t)(sy * sy) > deadzone_sq) {
            pad->stick_x = sx / 409;
            pad->stick_y = sy / 409;
        }
    }

    // the ext (camera) stick follows whichever stick drives the C-buttons
    if (ext_stick_source >= 0) {
        const int16_t ex = ext_stick_source == 0 ? leftx : rightx;
        const int16_t ey = ext_stick_source == 0 ? lefty : righty;
        uint32_t magnitude_sq = (uint32_t)(ex * ex) + (uint32_t)(ey * ey);
        if (magnitude_sq > deadzone_sq) {
            // Game expects stick coordinates within -80..80
            // 32768 / 409 = ~80
            pad->ext_stick_x = ex / 409;
            pad->ext_stick_y = -ey / 409;
        }
    }
}

static void controller_sdl_rumble_play(f32 strength, f32 length) {
    if (sdl_haptic) {
        SDL_HapticRumblePlay(sdl_haptic, strength, (u32)(length * 1000.0f));
    }
    else {
#if SDL_VERSION_ATLEAST(2,0,18)
        uint16_t scaled_strength = strength * pow(2, 16) - 1;
        if (SDL_GameControllerHasRumble(sdl_cntrl) == SDL_TRUE) {
            SDL_GameControllerRumble(sdl_cntrl, scaled_strength, scaled_strength, (u32)(length * 1000.0f));
        }
#endif
    }
}

static void controller_sdl_rumble_stop(void) {
    if (sdl_haptic) {
        SDL_HapticRumbleStop(sdl_haptic);
    }
    else {
#if SDL_VERSION_ATLEAST(2,0,18)
        if (SDL_GameControllerHasRumble(sdl_cntrl) == SDL_TRUE) {
            SDL_GameControllerRumble(sdl_cntrl, 0, 0, 0);
        }
#endif
    }
}

static u32 controller_sdl_rawkey(void) {
    if (last_joybutton != VK_INVALID) {
        const u32 ret = last_joybutton;
        last_joybutton = VK_INVALID;
        return ret;
    }

#ifdef MOUSE_ACTIONS
    if (configMouse) {
        for (int i = 0; i < MAX_MOUSEBUTTONS; ++i) {
            if (last_mouse & SDL_BUTTON(i)) {
                const u32 ret = VK_OFS_SDL_MOUSE + i;
                last_mouse = 0;
                return ret;
            }
        }
    }
#endif
    return VK_INVALID;
}

static void controller_sdl_shutdown(void) {
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        if (sdl_cntrl) {
            SDL_GameControllerClose(sdl_cntrl);
            sdl_cntrl = NULL;
        }
        if (sdl_joy) {
            SDL_JoystickClose(sdl_joy);
            sdl_joy = NULL;
        }
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }

    if (SDL_WasInit(SDL_INIT_HAPTIC)) {
        if (sdl_haptic) {
            SDL_HapticClose(sdl_haptic);
            sdl_haptic = NULL;
        }
        SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    }

    haptics_enabled = false;
    init_ok = false;
#ifdef MOUSE_ACTIONS
    mouse_init_ok = false;
#endif
}

struct ControllerAPI controller_sdl = {
    VK_BASE_SDL_GAMEPAD,
    controller_sdl_init,
    controller_sdl_read,
    controller_sdl_rawkey,
    controller_sdl_rumble_play,
    controller_sdl_rumble_stop,
    controller_sdl_bind,
    controller_sdl_shutdown
};

#endif // CAPI_SDL2
