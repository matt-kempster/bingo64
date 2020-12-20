/**
 * Behaviors for bhvPoleGrabbing and bhvGiantPole.
 *
 * bhvPoleGrabbing is a generic behavior used almost anywhere the player can
 * grab onto and climb up another object.
 *
 * bhvGiantPole controls the large pole in Whomp's Fortress and differs from
 * other poles because it spawns a yellow ball that rests at its top.
 */
#include "game/bingo.h"
#include "game/bingo_tracking_collectables.h"

/**
 * Initialize the hitbox height field for bhvPoleGrabbing.
 */
void bhv_pole_init(void) {
    /**
     * This is equivalent to using `o->oBehParams2ndByte` according to
     * `spawn_objects_from_info`.
     */
    s32 tenthHitboxHeight = o->oBehParams >> 0x10 & 0xFF;
    o->hitboxHeight = tenthHitboxHeight * 10;
    o->oBingoId = get_unique_id(BINGO_UPDATE_GRABBED_POLE, o->oPosX, o->oPosY, o->oPosZ);
}

/**
 * Main loop for bhvGiantPole. It is equivalent to BehClimbDetectLoop() except
 * for the fact that it spawns a yellow sphere at the top of the pole on the
 * first frame.
 */
void bhv_giant_pole_loop(void) {
    struct Object *topBall;
    if (o->oTimer == 0) {
        o->oBingoId = get_unique_id(BINGO_UPDATE_GRABBED_POLE, o->oPosX, o->oPosY, o->oPosZ);
        topBall = spawn_object(o, MODEL_YELLOW_SPHERE, bhvYellowBall);
        topBall->oPosY += o->hitboxHeight + 50.0f;
    }
    BehClimbDetectLoop();
}
