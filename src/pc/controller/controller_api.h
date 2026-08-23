#ifndef CONTROLLER_API
#define CONTROLLER_API

#define DEADZONE_STEP 310  // original deadzone is 4960
#define VK_INVALID 0xFFFF
#define VK_SIZE 0x1000

// virtual buttons for left and right analog triggers
#define VK_LTRIGGER 0x101A
#define VK_RTRIGGER 0x101B

// virtual buttons for the analog stick directions, so the sticks can be
// captured in the rebind menu and bound like buttons (SDL2 backend).
// Order matters: left stick then right, each up/down/left/right.
#define VK_LSTICK_UP    0x1020
#define VK_LSTICK_DOWN  0x1021
#define VK_LSTICK_LEFT  0x1022
#define VK_LSTICK_RIGHT 0x1023
#define VK_RSTICK_UP    0x1024
#define VK_RSTICK_DOWN  0x1025
#define VK_RSTICK_LEFT  0x1026
#define VK_RSTICK_RIGHT 0x1027

// fake buttons for binding the stick directions
#define STICK_UP    0x80000
#define STICK_DOWN  0x40000
#define STICK_LEFT  0x10000
#define STICK_RIGHT 0x20000
#define STICK_XMASK 0x30000
#define STICK_YMASK 0xc0000

#include <ultra64.h>

struct ControllerAPI {
   const u32 vkbase;                            // base number in the virtual keyspace (e.g. keyboard is 0x0000-0x1000)
    void (*init)(void);                         // call once, also calls reconfig()
    void (*read)(OSContPad *pad);               // read controller and update N64 pad values
    u32  (*rawkey)(void);                       // returns last pressed virtual key or VK_INVALID if none
    void (*rumble_play)(float str, float time); // (optional) rumble with intensity `str` (0 - 1) for `time` seconds
    void (*rumble_stop)(void);                  // (optional) stop any ongoing haptic feedback
    void (*reconfig)(void);                     // (optional) call when bindings have changed
    void (*shutdown)(void);                     // (optional) call in osContReset
};

// used for binding keys
u32 controller_get_raw_key(void);
void controller_reconfigure(void);

// rumbles all controllers with rumble support
void controller_rumble_play(float str, float time);
void controller_rumble_stop(void);

// calls the shutdown() function of all controller subsystems
void controller_shutdown(void);

#endif
