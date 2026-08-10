#ifndef _CLIOPTS_H
#define _CLIOPTS_H

#ifdef COMMAND_LINE_OPTIONS

#include "platform.h"

struct PCCLIOptions  {
    unsigned int SkipIntro;
    unsigned int FullScreen;
    unsigned int LevelSelect;
    unsigned int LevelNumOverride;
    unsigned int LevelActOverride;
    unsigned int Profiler;
    unsigned int Debug;
    unsigned int Mute;
    unsigned int PoolSize;
    char ConfigFile[SYS_MAX_PATH];
    char SavePath[SYS_MAX_PATH];
    char GameDir[SYS_MAX_PATH];
    // Online bingo (see server/relay.py)
    char NetServer[128];
    char NetRoom[32];
    char NetName[16];
    unsigned int NetColor;
};

extern struct PCCLIOptions gCLIOpts;

void parse_cli_opts(int argc, char* argv[]);
void set_cli_opts(void);

#endif

#endif // _CLIOPTS_H
