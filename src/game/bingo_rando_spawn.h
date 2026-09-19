#ifndef _BINGO_RANDO_SPAWN_H
#define _BINGO_RANDO_SPAWN_H

// Safe-position generation for the random-stars bingo objective.
//
// The core algorithm (bounding boxes per area, avoidance points, floor
// safety rules, and the wall-check raycaster) is adapted from JoshDuMan's
// SM64_Randomizer (https://github.com/JoshDuMan/SM64_Randomizer), used
// with the author's permission, and trimmed down to only what is needed
// to place a handful of stars in sensible spots. The full randomizer
// (object scrambling, warp scrambling, cosmetics, difficulty settings)
// is intentionally not included here.

#include <ultra64.h>
#include "types.h"

struct AvoidancePoint {
    Vec3s pos;
    f32 radius;
    f32 height;
};

typedef struct AvoidancePoint AvoidancePointArray[];

struct AreaParams {
    f32 minX;
    f32 maxX;
    f32 minY;
    f32 maxY;
    f32 minZ;
    f32 maxZ;

    u32 areaParamFlags;

    f32 wallCheckRaycasterSearchDist;

    u32 numAvoidancePoints;
    AvoidancePointArray *avoidancePoints;
};

typedef struct AreaParams AreaParamsArray[];

enum FloorSafeLevels {
    FLOOR_SAFE_GROUNDED,
    FLOOR_SAFE_HOVERING,
    FLOOR_SAFE_START_WARP
};

#define RAND_POSITION_FLAG_CAN_BE_UNDERWATER (1 << 0)
#define RAND_POSITION_FLAG_THI_A3_ABOVE_MESH (1 << 1)
#define RAND_POSITION_FLAG_SPAWN_TOP_OF_SLIDE (1 << 2)
#define RAND_POSITION_FLAG_SPAWN_BOTTOM_OF_SLIDE (1 << 3)
#define RAND_POSITION_FLAG_BBH_HMC_LIMITED_ROOMS (1 << 4)
#define RAND_POSITION_FLAG_SPAWN_FAR_FROM_WALLS (1 << 5)
#define RAND_POSITION_FLAG_SAFE (1 << 6)
#define RAND_POSITION_FLAG_MUST_BE_UNDERWATER (1 << 7)

#define AREA_PARAM_FLAG_CHANGING_WATER_LEVEL (1 << 0)

void get_safe_position(struct Object *obj, Vec3s pos, f32 minHeightRange, f32 maxHeightRange,
                       u16 *seed, u8 floorSafeLevel, u8 randPosFlags);
s32 bingo_rando_area_count(s32 levelNum);

#endif /* _BINGO_RANDO_SPAWN_H */
