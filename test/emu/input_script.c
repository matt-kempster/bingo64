// Mupen64Plus input plugin that plays back a scripted controller recording.
//
// The script is a text file named by the BINGO_INPUT_SCRIPT env var.
// Each line holds a button or the stick over a frame range:
//
//   # press start from frame 100 up to (not including) frame 105
//   100 105 START
//   200 260 A
//   300 400 STICK -80 0
//
// Buttons: A B Z START L R DU DD DL DR CU CD CL CR
// Frames count controller polls since the ROM was opened, which lines up
// with video frames. Same ROM + same script = same run, every time.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m64p_types.h"
#include "m64p_plugin.h"

#define PLUGIN_NAME "bingo64 scripted input"
#define PLUGIN_VERSION 0x010000
#define INPUT_PLUGIN_API_VERSION 0x020100

struct ScriptEvent {
    long start;
    long end;
    unsigned int mask;
    int isStick;
    int x, y;
};

#define MAX_EVENTS 4096
static struct ScriptEvent sEvents[MAX_EVENTS];
static int sNumEvents = 0;
static long sFrame = 0;
static void (*sDebugCallback)(void *, int, const char *) = NULL;
static void *sDebugContext = NULL;

static void log_msg(int level, const char *msg) {
    if (sDebugCallback != NULL) {
        sDebugCallback(sDebugContext, level, msg);
    }
}

static unsigned int button_mask(const char *name) {
    static const struct { const char *name; int bit; } table[] = {
        { "DR", 0 }, { "DL", 1 }, { "DD", 2 }, { "DU", 3 },
        { "START", 4 }, { "Z", 5 }, { "B", 6 }, { "A", 7 },
        { "CR", 8 }, { "CL", 9 }, { "CD", 10 }, { "CU", 11 },
        { "R", 12 }, { "L", 13 },
    };
    size_t i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(name, table[i].name) == 0) {
            return 1u << table[i].bit;
        }
    }
    return 0;
}

static void load_script(void) {
    const char *path = getenv("BINGO_INPUT_SCRIPT");
    char line[256], token[32];
    long start, end;
    int x, y;
    FILE *f;

    sNumEvents = 0;
    if (path == NULL || path[0] == '\0') {
        log_msg(M64MSG_INFO, "no BINGO_INPUT_SCRIPT set; controller stays idle");
        return;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        log_msg(M64MSG_ERROR, "cannot open BINGO_INPUT_SCRIPT file");
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL && sNumEvents < MAX_EVENTS) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
            continue;
        }
        if (sscanf(line, "%ld %ld STICK %d %d", &start, &end, &x, &y) == 4) {
            sEvents[sNumEvents].start = start;
            sEvents[sNumEvents].end = end;
            sEvents[sNumEvents].isStick = 1;
            sEvents[sNumEvents].x = x;
            sEvents[sNumEvents].y = y;
            sEvents[sNumEvents].mask = 0;
            sNumEvents++;
        } else if (sscanf(line, "%ld %ld %31s", &start, &end, token) == 3) {
            unsigned int mask = button_mask(token);
            if (mask == 0) {
                log_msg(M64MSG_WARNING, "unknown button in input script");
                continue;
            }
            sEvents[sNumEvents].start = start;
            sEvents[sNumEvents].end = end;
            sEvents[sNumEvents].isStick = 0;
            sEvents[sNumEvents].mask = mask;
            sNumEvents++;
        }
    }
    fclose(f);
    log_msg(M64MSG_INFO, "input script loaded");
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *)) {
    (void) CoreLibHandle;
    sDebugContext = Context;
    sDebugCallback = DebugCallback;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void) {
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion,
                                        int *APIVersion, const char **PluginNamePtr,
                                        int *Capabilities) {
    if (PluginType != NULL) *PluginType = M64PLUGIN_INPUT;
    if (PluginVersion != NULL) *PluginVersion = PLUGIN_VERSION;
    if (APIVersion != NULL) *APIVersion = INPUT_PLUGIN_API_VERSION;
    if (PluginNamePtr != NULL) *PluginNamePtr = PLUGIN_NAME;
    if (Capabilities != NULL) *Capabilities = 0;
    return M64ERR_SUCCESS;
}

EXPORT void CALL InitiateControllers(CONTROL_INFO ControlInfo) {
    int i;
    for (i = 0; i < 4; i++) {
        ControlInfo.Controls[i].Present = (i == 0);
        ControlInfo.Controls[i].RawData = 0;
        ControlInfo.Controls[i].Plugin = PLUGIN_NONE;
    }
}

EXPORT void CALL GetKeys(int Control, BUTTONS *Keys) {
    int i;
    if (Control != 0) {
        Keys->Value = 0;
        return;
    }
    Keys->Value = 0;
    for (i = 0; i < sNumEvents; i++) {
        if (sFrame >= sEvents[i].start && sFrame < sEvents[i].end) {
            if (sEvents[i].isStick) {
                Keys->X_AXIS = sEvents[i].x;
                Keys->Y_AXIS = sEvents[i].y;
            } else {
                Keys->Value |= sEvents[i].mask;
            }
        }
    }
    sFrame++;
}

EXPORT int CALL RomOpen(void) {
    sFrame = 0;
    load_script();
    return 1;
}

EXPORT void CALL RomClosed(void) {
}

EXPORT void CALL ControllerCommand(int Control, unsigned char *Command) {
    (void) Control;
    (void) Command;
}

EXPORT void CALL ReadController(int Control, unsigned char *Command) {
    (void) Control;
    (void) Command;
}

EXPORT void CALL SDL_KeyDown(int keymod, int keysym) {
    (void) keymod;
    (void) keysym;
}

EXPORT void CALL SDL_KeyUp(int keymod, int keysym) {
    (void) keymod;
    (void) keysym;
}
