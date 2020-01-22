#ifndef _BINGO_H
#define _BINGO_H

#define ICON_STAR "-"
#define ICON_COIN "+"
#define ICON_BOBOMB "\""
#define ICON_PURPLESTAR "$"
#define ICON_1UP "\'"
#define ICON_A_BUTTON ")"
#define ICON_TIMER "("
#define ICON_GOOMBA "/"
#define ICON_B_BUTTON "^"
#define ICON_Z_BUTTON "_"
#define ICON_MULTICOIN  "`"
#define ICON_YELLOW_EXCLAMATION_MARK_BOX  "\x1E"

#define ICON_COMPLETE "O"
#define ICON_FAILED ":"

#include "course_table.h"

// Global bingo state information
extern s32 gBingoInitialized;
extern u32 gBingoInitialSeed;
extern s64 gbGlobalBingoTimer;
extern s32 gbBingoCompleted;
extern s32 gbBingoShowTimer;

extern s16 gbStarIndex;
extern s32 gbCoinsJustGotten;

// TODO: Kill this. It can be passed as a parameter
extern enum BingoObjectiveUpdate sBingoCurrUpdate;

enum BingoObjectiveState
{
    BINGO_STATE_NONE,
    BINGO_STATE_COMPLETE,
    BINGO_STATE_FAILED_IN_THIS_COURSE
};

enum BingoObjectiveClass
{
    // Difficulty-based classes:
    BINGO_CLASS_EASY,
    BINGO_CLASS_MEDIUM,
    BINGO_CLASS_HARD,
    // Positional classes:
    BINGO_CLASS_CENTER
};

enum BingoObjectiveType
{
    BINGO_OBJECTIVE_TYPE_MIN,
    // begin
    BINGO_OBJECTIVE_STAR = BINGO_OBJECTIVE_TYPE_MIN,
    BINGO_OBJECTIVE_STAR_A_BUTTON_CHALLENGE,
    BINGO_OBJECTIVE_STAR_B_BUTTON_CHALLENGE,
    BINGO_OBJECTIVE_STAR_Z_BUTTON_CHALLENGE,
    BINGO_OBJECTIVE_STAR_TIMED, // TODO: this should maybe be a flag to stack objs!
    BINGO_OBJECTIVE_COIN,
    BINGO_OBJECTIVE_MULTICOIN,
    BINGO_OBJECTIVE_1UPS_IN_LEVEL,
    BINGO_OBJECTIVE_STARS_IN_LEVEL,
    BINGO_OBJECTIVE_EXCLAMATION_MARK_BOX,
    BINGO_OBJECTIVE_KILL_ENEMIES,
    // end
    BINGO_OBJECTIVE_TYPE_MAX = BINGO_OBJECTIVE_KILL_ENEMIES
};

enum BingoObjectiveUpdate
{
    BINGO_UPDATE_STAR,
    BINGO_UPDATE_COIN,
    BINGO_UPDATE_KILLED_GOOMBA,
    BINGO_UPDATE_KILLED_BOBOMB,
    BINGO_UPDATE_COURSE_CHANGED,
    BINGO_UPDATE_A_PRESSED,
    BINGO_UPDATE_B_PRESSED,
    BINGO_UPDATE_Z_PRESSED,
    BINGO_UPDATE_TIMER_FRAME,
    BINGO_UPDATE_GOT_1UP,
    BINGO_UPDATE_EXCLAMATION_MARK_BOX,
    BINGO_UPDATE_RESET_TIMER
};

enum EnemyType
{
    ENEMY_TYPE_GOOMBA,
    ENEMY_TYPE_BOBOMB
};

// TODO: Move this ito BingoObjectiveUpdate as a range within the enum.
static s32 update_is_enemy_killing(enum BingoObjectiveUpdate update)
{
    return (
        update == BINGO_UPDATE_KILLED_GOOMBA ||
        update == BINGO_UPDATE_KILLED_BOBOMB
    );
}

static s32 update_type_matches_enemy_type(
    enum BingoObjectiveUpdate update,
    enum EnemyType type
) {
    return (
        (update == BINGO_UPDATE_KILLED_GOOMBA && type == ENEMY_TYPE_GOOMBA) ||
        (update == BINGO_UPDATE_KILLED_BOBOMB && type == ENEMY_TYPE_BOBOMB)
    );
}

struct StarObjectiveData
{
    enum CourseNum course;
    s32 starIndex;
};

struct ABCStarObjectiveData
{
    enum CourseNum course;
    s32 starIndex;
    char *hint;
};

struct StarTimerObjectiveData
{
    enum CourseNum course;
    s32 starIndex;
    s32 maxTime;
    s32 timer;
};

struct KillEnemyObjectiveData
{
    enum EnemyType enemyType;
    s32 enemiesToKill;
    s32 enemiesKilled;
};

struct CourseCollectableData
{
    enum CourseNum course;
    s32 toGet;
    s32 gotten;
};

struct CollectableData
{
    s32 toGet;
    s32 gotten;
};

struct BingoObjective
{
    enum BingoObjectiveType type;
    char *(*discrip_func)(struct BingoObjective *, char *);
    enum BingoObjectiveState state;
    char icon[2];
    char title[30];
    union {
        struct StarObjectiveData starObjective;
        struct ABCStarObjectiveData abcStarObjective;
        struct StarTimerObjectiveData starTimerObjective;
        struct CourseCollectableData courseCollectableData;
        struct KillEnemyObjectiveData killEnemyObjective;
        struct CollectableData collectableData;
    } data;
};

extern struct BingoObjective gBingoObjectives[25];

void set_objective_state(struct BingoObjective *objective, enum BingoObjectiveState state);
void bingo_update(enum BingoObjectiveUpdate update);

#endif /* _BINGO_H */
