#include <ultra64.h>

#include "sm64.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "area.h"
#include "engine/math_util.h"
#include "engine/rand.h"
#include "engine/behavior_script.h"
#include "macros.h"
#include "course_table.h"
#include "level_table.h"

#include "game/bingo_rando_spawn.h"

// See the header for provenance. Data tables and constants below carry the
// upstream randomizer's 1.0.1-era tuning (Oct 2022), minus the avoidance
// points that only make sense when scrambling objects other than stars.

#define ABSF(x) ((x) > 0 ? (x) : -(x))

#define WALL_CHECK_RAYCASTER_NUM_RAYS_TO_CAST 6
#define WALL_CHECK_RAYCASTER_STEP_SIZE 100.0f
#define WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST 500.0f

static u32 sAreaIndex;

struct AvoidancePoint wfAvoidancePoints[] = {
    { { 0, 3830, 710 }, 300.0f, 50.0f },   // WF tower platform
    { { 710, 4030, 0 }, 300.0f, 50.0f },   // WF tower platform
    { { 0, 4230, -710 }, 300.0f, 50.0f },  // WF tower platform
    { { -710, 4430, 0 }, 300.0f, 50.0f },  // WF tower platform
};

struct AvoidancePoint ccmAvoidancePoints[] = {
    { { -4230, -1250, 1813 }, 200.0f, 200.0f },  // Snowman's body platform
};

struct AvoidancePoint bbhAvoidancePoints[] = {
    { { 0, 100, 0 }, 200.0f, 200.0f },  // Inside the BBH pillar in the main room
};

struct AvoidancePoint lllAvoidancePoints[] = {
    { { -3200, 110, 3456 }, 200.0f, 150.0f },  // Under the Mr. I vanilla spawn
};

struct AvoidancePoint sslPyrAvoidancePoints[] = {
    { { 0, 5062, 256 }, 600.0f, 200.0f },     // In elevator
    { { 0, 300, -1400 }, 1000.0f, 500.0f },   // Past Eyerok loading zone
};

struct AvoidancePoint dddAvoidancePoints[] = {
    { { -3177, -4600, 100 }, 650.0f, 150.0f },  // Under whirlpool
};

struct AvoidancePoint wdwAvoidancePoints[] = {
    { { 4069, 0, -3339 }, 800.0f, 1200.0f },  // In cage
};

struct AvoidancePoint wdwTownAvoidancePoints[] = {
    { { 2254, -2559, 894 }, 1200.0f, 650.0f },     // In a building
    { { -3583, -2508, -2047 }, 200.0f, 200.0f },   // Over the lowermost water tap
};

struct AvoidancePoint bitfsAvoidancePoints[] = {
    { { 6772, 2900, 106 }, 500.0f, 50.0f },  // In Bowser warp
};

struct AvoidancePoint icBasAvoidancePoints[] = {
    { { 6000, -1074, 2000 }, 1000.0f, 50.0f },  // Behind DDD painting
    { { -1023, -1074, 589 }, 400.0f, 50.0f },   // Behind first key door
};

struct AvoidancePoint icUpAvoidancePoints[] = {
    { { -1963, 2240, 4815 }, 1000.0f, 100.0f },  // RR side under first stair
    { { 1250, 2240, 4815 }, 1000.0f, 100.0f },   // WMotR side under first stair
};

// Main courses
struct AreaParams bobParams[] = {
    {-8192, 8192, 0, 4500, -8192, 8192, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // BoB
};
struct AreaParams wfParams[] = {
    {-3300, 5100, 256, 6500, -4100, 5700, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, ARRAY_COUNT(wfAvoidancePoints), &wfAvoidancePoints} // WF
};
struct AreaParams jrbParams[] = {
    {-7800, 8000, -5200, 3000, -7500, 8000, 0, 1000, 0, NULL}, // JRB main area
    {-1000, 1200, -300, 1500, -3000, 3500, AREA_PARAM_FLAG_CHANGING_WATER_LEVEL, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // JRB sunken ship
};
struct AreaParams ccmParams[] = {
    {-6500, 6500, -4600, 4600, -5500, 6500, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, ARRAY_COUNT(ccmAvoidancePoints), &ccmAvoidancePoints}, // CCM main area
    {-7500, 7500, -5800, 7400, -7000, 7000, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // CCM slide
};
struct AreaParams bbhParams[] = {
    {-4200, 5500, -3000, 3500, -4000, 6700, 0, 800, ARRAY_COUNT(bbhAvoidancePoints), &bbhAvoidancePoints} // BBH
};
struct AreaParams hmcParams[] = {
    {-7800, 7600, -6300, 3000, -8192, 8192, 0, 800, 0, NULL} // HMC
};
struct AreaParams lllParams[] = {
    {-8192, 8192, 0, 1600, -8192, 8192, 0, 300, ARRAY_COUNT(lllAvoidancePoints), &lllAvoidancePoints}, // LLL main area
    {-3000, 3000, 0, 5000, -3000, 3000, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // LLL volcano
};
struct AreaParams sslParams[] = {
    {-8192, 8192, -250, 2000, -8192, 8192, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL}, // SSL main area
    {-4000, 4000, -250, 5500, -4200, 6600, 0, 700, ARRAY_COUNT(sslPyrAvoidancePoints), &sslPyrAvoidancePoints}, // SSL pyramid
    {-1000, 1000, -1500, -500, -4000, -1800, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // SSL eyerok
};
struct AreaParams dddParams[] = {
    {-6500, 2000, -5200, 1000, -3500, 3500, 0, 1000, ARRAY_COUNT(dddAvoidancePoints), &dddAvoidancePoints}, // DDD starting area
    {-1500, 7000, -4000, 1600, -4000, 6000, 0, 1000, 0, NULL} // DDD sub area
};
struct AreaParams slParams[] = {
    {-7891, 6868, 800, 5500, -7891, 7892, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL}, // SL main area
    {-2000, 2000, 0, 650, -2000, 2500, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // SL igloo
};
struct AreaParams wdwParams[] = {
    {-3800, 4500, 0, 5000, -3800, 4500, AREA_PARAM_FLAG_CHANGING_WATER_LEVEL, 800, ARRAY_COUNT(wdwAvoidancePoints), &wdwAvoidancePoints}, // WDW starting area
    {-3800, 2300, -2500, 250, -2300, 3800, AREA_PARAM_FLAG_CHANGING_WATER_LEVEL, 1200, ARRAY_COUNT(wdwTownAvoidancePoints), &wdwTownAvoidancePoints} // WDW downtown
};
struct AreaParams ttmParams[] = {
    {-4100, 5300, -4500, 3200, -5300, 6700, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL}, // TTM main area
    {-2600, 8192, -200, 8192, -2900, 8192, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL}, // TTM slide first section
    {-8192, 8192, -7000, 6300, -8192, 8192, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL}, // TTM slide second section
    {-8192, 8192, -8192, 5500, -8192, 8192, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // TTM slide third section
};
struct AreaParams thiParams[] = {
    {-8192, 8192, -3400, 5000, -8192, 8192, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL}, // THI huge island
    {-2500, 2500, -1100, 2000, -5000, 2500, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL}, // THI small island
    {-2000, 2000, 500, 2500, -2000, 2000, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // THI wiggler's cave
};
struct AreaParams ttcParams[] = {
    {-2000, 3000, -5200, 7500, -2000, 3000, 0, 500, 0, NULL} // TTC
};
struct AreaParams rrParams[] = {
    {-8000, 7500, -4600, 7200, -7000, 7500, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // RR
};

// Secret courses
struct AreaParams pssParams[] = {
    {-7700, 6000, -4500, 7500, -7200, 6600, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // PSS
};
struct AreaParams saParams[] = {
    {-2750, 2750, -4250, -250, -2750, 2750, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // SA
};
struct AreaParams wmotrParams[] = {
    {-4200, 4600, -2750, 5500, -5200, 6000, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // WMotR
};
struct AreaParams totwcParams[] = {
    {-1500, 1500, -2100, -500, -1500, 1500, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // TotWC
};
struct AreaParams cotmcParams[] = {
    {-5000, 1400, -600, 1500, -7500, 2000, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // CotMC
};
struct AreaParams vcutmParams[] = {
    {-6500, 5000, -3300, 6500, -6500, 1700, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // VCutM
};
struct AreaParams bitdwParams[] = {
    {-8000, 7500, -3400, 3800, -3000, 4400, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // BitDW
};
struct AreaParams bitfsParams[] = {
    {-8000, 8000, -3000, 6000, -2500, 1500, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, ARRAY_COUNT(bitfsAvoidancePoints), &bitfsAvoidancePoints} // BitFS
};
struct AreaParams bitsParams[] = {
    {-7500, 7600, -5000, 7000, -7000, 500, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // BitS
};

// Castle areas
struct AreaParams cgParams[] = {
    {-8192, 8192, -500, 7500, -7000, 7000, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // Castle grounds
};
struct AreaParams ccParams[] = {
    {-3700, 3700, -200, 500, -3800, 500, 0, WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST, 0, NULL} // Castle courtyard
};
struct AreaParams icParams[] = {
    {-7500, 4300, -1000, 600, -5000, 2200, 0, 1000, 0, NULL}, // Castle foyer
    {-7300, 4300, 500, 5300, -3700, 7000, 0, 2000, ARRAY_COUNT(icUpAvoidancePoints), &icUpAvoidancePoints}, // Castle upstairs
    {-4200, 7800, -2500, -500, -4000, 3000, 0, 1000, ARRAY_COUNT(icBasAvoidancePoints), &icBasAvoidancePoints} // Castle basement
};

// Indexed by (gCurrLevelNum - 4).
AreaParamsArray *sLevelParams[] = {
    &bbhParams,
    &ccmParams,
    &icParams,
    &hmcParams,
    &sslParams,
    &bobParams,
    &slParams,
    &wdwParams,
    &jrbParams,
    &thiParams,
    &ttcParams,
    &rrParams,
    &cgParams,
    &bitdwParams,
    &vcutmParams,
    &bitfsParams,
    &saParams,
    &bitsParams,
    &lllParams,
    &dddParams,
    &wfParams,
    NULL,
    &ccParams,
    &pssParams,
    &cotmcParams,
    &totwcParams,
    NULL,
    &wmotrParams,
    NULL,
    NULL,
    NULL,
    NULL,
    &ttmParams
};

// Number of AreaParams entries per level. Must stay in the same order as
// sLevelParams above; ARRAY_COUNT keeps each entry current when areas are
// added to a table.
static u8 sLevelAreaCounts[] = {
    ARRAY_COUNT(bbhParams),
    ARRAY_COUNT(ccmParams),
    ARRAY_COUNT(icParams),
    ARRAY_COUNT(hmcParams),
    ARRAY_COUNT(sslParams),
    ARRAY_COUNT(bobParams),
    ARRAY_COUNT(slParams),
    ARRAY_COUNT(wdwParams),
    ARRAY_COUNT(jrbParams),
    ARRAY_COUNT(thiParams),
    ARRAY_COUNT(ttcParams),
    ARRAY_COUNT(rrParams),
    ARRAY_COUNT(cgParams),
    ARRAY_COUNT(bitdwParams),
    ARRAY_COUNT(vcutmParams),
    ARRAY_COUNT(bitfsParams),
    ARRAY_COUNT(saParams),
    ARRAY_COUNT(bitsParams),
    ARRAY_COUNT(lllParams),
    ARRAY_COUNT(dddParams),
    ARRAY_COUNT(wfParams),
    1,
    ARRAY_COUNT(ccParams),
    ARRAY_COUNT(pssParams),
    ARRAY_COUNT(cotmcParams),
    ARRAY_COUNT(totwcParams),
    1,
    ARRAY_COUNT(wmotrParams),
    1,
    1,
    1,
    1,
    ARRAY_COUNT(ttmParams)
};

s32 bingo_rando_area_count(s32 levelNum) {
    if (levelNum < 4 || levelNum - 4 >= (s32) ARRAY_COUNT(sLevelParams)
        || sLevelParams[levelNum - 4] == NULL) {
        return 1;
    }
    return sLevelAreaCounts[levelNum - 4];
}

// Only uniform if used for floats. [min, max)
// Draws from the global MT, which get_safe_position seeds; the old
// self-feeding u16 scheme (reseed from the previous draw) collapsed into a
// short cycle, so sparse-floor levels could search the same few candidate
// positions forever and hang on level load.
static f32 get_val_in_range_uniform(f32 min, f32 max) {
    if (min > max)
        return min;

    return (random_u16() / (double) 0x10000 * (max - min)) + min;
}

static f32 sWallCheckRaycasterSearchDist = WALL_CHECK_RAYCASTER_DEFAULT_SEARCH_DIST;
static u8 sWallCheckNumRaySteps = 0;

static Vec3f sRaycastDirections[WALL_CHECK_RAYCASTER_NUM_RAYS_TO_CAST];
static u8 sRaycasterInitialized = FALSE;

static struct Surface *return_first_wall_collision(struct SurfaceNode *surfaceNode, struct WallCollisionData *data)
{
    register f32 offset;
    register f32 radius = data->radius;
    struct Surface *surf;
    register f32 x = data->x;
    register f32 y = data->y;
    register f32 z = data->z;
    register f32 px, pz;
    register f32 w1, w2, w3;
    register f32 y1, y2, y3;

    while (surfaceNode != NULL)
    {
        surf = surfaceNode->surface;
        surfaceNode = surfaceNode->next;

        if (y < surf->lowerY || y > surf->upperY)
            continue;

        offset = surf->normal.x * x + surf->normal.y * y + surf->normal.z * z + surf->originOffset;

        if (offset < -radius || offset > radius)
            continue;

        px = x;
        pz = z;

        if (surf->flags & SURFACE_FLAG_X_PROJECTION)
        {
            w1 = -surf->vertex1[2];
            w2 = -surf->vertex2[2];
            w3 = -surf->vertex3[2];
            y1 = surf->vertex1[1];
            y2 = surf->vertex2[1];
            y3 = surf->vertex3[1];

            if (surf->normal.x > 0.0f)
            {
                if ((y1 - y) * (w2 - w1) - (w1 - -pz) * (y2 - y1) > 0.0f) continue;
                if ((y2 - y) * (w3 - w2) - (w2 - -pz) * (y3 - y2) > 0.0f) continue;
                if ((y3 - y) * (w1 - w3) - (w3 - -pz) * (y1 - y3) > 0.0f) continue;
            }
            else
            {
                if ((y1 - y) * (w2 - w1) - (w1 - -pz) * (y2 - y1) < 0.0f) continue;
                if ((y2 - y) * (w3 - w2) - (w2 - -pz) * (y3 - y2) < 0.0f) continue;
                if ((y3 - y) * (w1 - w3) - (w3 - -pz) * (y1 - y3) < 0.0f) continue;
            }
        }
        else
        {
            w1 = surf->vertex1[0];
            w2 = surf->vertex2[0];
            w3 = surf->vertex3[0];
            y1 = surf->vertex1[1];
            y2 = surf->vertex2[1];
            y3 = surf->vertex3[1];

            if (surf->normal.z > 0.0f)
            {
                if ((y1 - y) * (w2 - w1) - (w1 - px) * (y2 - y1) > 0.0f) continue;
                if ((y2 - y) * (w3 - w2) - (w2 - px) * (y3 - y2) > 0.0f) continue;
                if ((y3 - y) * (w1 - w3) - (w3 - px) * (y1 - y3) > 0.0f) continue;
            }
            else
            {
                if ((y1 - y) * (w2 - w1) - (w1 - px) * (y2 - y1) < 0.0f) continue;
                if ((y2 - y) * (w3 - w2) - (w2 - px) * (y3 - y2) < 0.0f) continue;
                if ((y3 - y) * (w1 - w3) - (w3 - px) * (y1 - y3) < 0.0f) continue;
            }
        }

        if (surf->type == SURFACE_CAMERA_BOUNDARY)
            continue;

        return surf;
    }

    return NULL;
}

static struct Surface *spawn_find_wall_collisions(struct WallCollisionData *colData)
{
    s16 cellX, cellZ;
    s16 x = colData->x;
    s16 z = colData->z;
    struct Surface *surf;

    // World (level) consists of a 16x16 grid. Find where the collision is on
    // the grid (round toward -inf)
    cellX = ((x + 0x2000) / 0x400) & 0x0F;
    cellZ = ((z + 0x2000) / 0x400) & 0x0F;

    surf = return_first_wall_collision(gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next, colData);

    if (surf == NULL)
    {
        surf = return_first_wall_collision(gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next, colData);
    }

    return surf;
}

static void vec3s_resolve_wall_collisions(Vec3s pos, f32 radius)
{
    Vec3f pos2;

    vec3s_to_vec3f(pos2, pos);
    f32_find_wall_collision(&pos2[0], &pos2[1], &pos2[2], 0.0f, radius);
    vec3f_to_vec3s(pos, pos2);
}

static void init_raycaster(void)
{
    u32 ray;
    u32 angleBetweenRays = 0x10000 / WALL_CHECK_RAYCASTER_NUM_RAYS_TO_CAST;

    // Fill sRaycastDirections
    for (ray = 0; ray < WALL_CHECK_RAYCASTER_NUM_RAYS_TO_CAST; ray++) {
        sRaycastDirections[ray][0] = WALL_CHECK_RAYCASTER_STEP_SIZE * coss(ray * angleBetweenRays);
        sRaycastDirections[ray][1] = 0.0f;
        sRaycastDirections[ray][2] = WALL_CHECK_RAYCASTER_STEP_SIZE * sins(ray * angleBetweenRays);
    }

    sRaycasterInitialized = TRUE;
}

static u8 is_safe_near_walls(Vec3s pos, u8 killOnOob)
{
    /*
        1. cast WALL_CHECK_RAYCASTER_NUM_RAYS_TO_CAST rays out evenly from pos until they hit ceiling, oob, or a wall
        2. discard the ones that hit ceiling or oob
        3. for each ray, take the dot product of the ray/direction vector with the wall's normal
        4. if there is a positive dot product, return false
        5. return true
    */

    register s8 i, j;

    register f32 rayX;
    register f32 rayY = pos[1] + 15;
    register f32 rayZ;

    register f32 directionX;
    register f32 directionZ;

    struct Surface *lowFloor, *highFloor, *ceil;
    register f32 floorHeight, ceilHeight, upperFloorHeight;

    struct WallCollisionData wallCollision;
    struct Surface *rayWall = NULL;

    register f32 normX, normZ;

    // Initialize raycaster if necessary
    if (sRaycasterInitialized == FALSE)
        init_raycaster();

    wallCollision.x = pos[0],
    wallCollision.z = pos[2],

    // Rays only move in X or Z directions, since walls are vertical, so we dont need to update this
    wallCollision.y = rayY,

    // Thickness of the walls
    wallCollision.radius = 75.0f;

    // If the point is already in a wall, it is not safe
    if (spawn_find_wall_collisions(&wallCollision) != NULL)
        return FALSE;

    // Raycast
    for (i = -1; ++i < WALL_CHECK_RAYCASTER_NUM_RAYS_TO_CAST;) {
        // Create ray
        rayX = pos[0],
        rayZ = pos[2];

        // Create direction vector
        directionX = sRaycastDirections[i][0],
        directionZ = sRaycastDirections[i][2];

        // Check if ray is in a wall yet, if not, advance it until it hits the limit
        // If it ends up in a ceiling or OoB, disregard the ray
        // Then check the wall's normal to determine if the ray collided with the back of it
        for (j = -1; ++j < sWallCheckNumRaySteps;) {
            rayX += directionX;
            rayZ += directionZ;

            floorHeight = find_floor(rayX, rayY, rayZ, &lowFloor);

            // Check if the ray is in a ceiling
            ceilHeight = find_ceil(rayX, floorHeight + 80, rayZ, &ceil);
            if (ceilHeight < (rayY + 100)) {
                break;
            }

            // See if the ray passed under a floor
            upperFloorHeight = find_floor(rayX, ceilHeight - 80, rayZ, &highFloor);
            if (upperFloorHeight > rayY && (upperFloorHeight - rayY) < 750) {
                break;
            }

            // Check if the ray is in OoB
            if (lowFloor == NULL) {
                if (killOnOob) {
                    return FALSE;
                }

                break;
            }

            // Create collision parameters
            wallCollision.x = rayX,
            wallCollision.z = rayZ;

            // Check for walls
            if ((rayWall = spawn_find_wall_collisions(&wallCollision)) != NULL)
                break;
        }

        // Check if the ray collided with the back of the wall
        if (rayWall == NULL)
            continue;

        normX = rayWall->normal.x,
        normZ = rayWall->normal.z;

        // Take dot product; positive means the ray is aligned with the wall normal,
        // and is therefore behind it.
        if (((rayX - pos[0]) * normX + (rayZ - pos[2]) * normZ) > 0)
            return FALSE;
    }

    return TRUE;
}

static u8 sSafeFloorsGeneral[30] = {
    SURFACE_DEFAULT,
    SURFACE_VERY_SLIPPERY,
    SURFACE_SLIPPERY,
    SURFACE_NOT_SLIPPERY,
    SURFACE_HARD,
    SURFACE_HARD_VERY_SLIPPERY,
    SURFACE_HARD_SLIPPERY,
    SURFACE_ICE,
    SURFACE_HARD_NOT_SLIPPERY,
    SURFACE_HORIZONTAL_WIND,
    SURFACE_FLOWING_WATER,
    SURFACE_MGR_MUSIC,
    SURFACE_NOISE_DEFAULT,
    SURFACE_NOISE_SLIPPERY,
    SURFACE_NOISE_VERY_SLIPPERY,
    SURFACE_CLOSE_CAMERA,      // Default floor with camera behavior
    SURFACE_WATER,             // Surface in fountain in courtyard
    SURFACE_SHALLOW_QUICKSAND, // Non-lethal, used in TTM/THI
    SURFACE_LOOK_UP_WARP,      // Sun carpet in lobby
    SURFACE_TIMER_START,       // Start of PSS
    SURFACE_TIMER_END,         // End of PSS
    SURFACE_BOSS_FIGHT_CAMERA, // Surfaces 65-70 are surfaces that are default type with camera behavior
    SURFACE_CAMERA_FREE_ROAM,
    SURFACE_THI3_WALLKICK,
    SURFACE_CAMERA_8_DIR,
    SURFACE_CAMERA_MIDDLE,
    SURFACE_CAMERA_ROTATE_RIGHT,
    SURFACE_CAMERA_ROTATE_LEFT,
    SURFACE_NO_CAM_COLLISION,
    SURFACE_NO_CAM_COL_SLIPPERY
};

static s32 find_floor_slipperiness(struct Surface *floor) {
    s32 floorClass = SURFACE_CLASS_DEFAULT;

    if (floor) {
        switch (floor->type) {
            case SURFACE_NOT_SLIPPERY:
            case SURFACE_HARD_NOT_SLIPPERY:
                floorClass = SURFACE_CLASS_NOT_SLIPPERY;
                break;

            case SURFACE_SLIPPERY:
            case SURFACE_NOISE_SLIPPERY:
            case SURFACE_HARD_SLIPPERY:
            case SURFACE_NO_CAM_COL_SLIPPERY:
                floorClass = SURFACE_CLASS_SLIPPERY;
                break;

            case SURFACE_VERY_SLIPPERY:
            case SURFACE_ICE:
            case SURFACE_HARD_VERY_SLIPPERY:
            case SURFACE_NOISE_VERY_SLIPPERY:
                floorClass = SURFACE_CLASS_VERY_SLIPPERY;
                break;
        }
    }

    return floorClass;
}

static u8 is_floor_safe(struct Surface *floor, u8 floorSafeLevel,
                        u8 randPosFlags) // Checks if floor triangle can be spawned on
{
    int i;
    s32 slipperiness;

    if (((floorSafeLevel == FLOOR_SAFE_GROUNDED) || (randPosFlags & RAND_POSITION_FLAG_SAFE)
         || (gCurrLevelNum == LEVEL_DDD))
        && (floor->flags & SURFACE_FLAG_DYNAMIC))
        return FALSE; // grounded objects / DDD objects can't spawn on platforms

    if (floor->normal.y
        > (floorSafeLevel != FLOOR_SAFE_HOVERING ? 0.9 : 0.3)) // Check steepness of floor
    {
        slipperiness = find_floor_slipperiness(floor);
        if (((floorSafeLevel == FLOOR_SAFE_START_WARP) || (randPosFlags & RAND_POSITION_FLAG_SAFE))
            && ((slipperiness == SURFACE_CLASS_SLIPPERY)
                || (slipperiness == SURFACE_CLASS_VERY_SLIPPERY))) {

            // This code kills some spawns, assuming the most slippery case. This code would
            // probably be better to refactor based off slipperiness in general.
            if (floor->normal.y <= 0.9848077f) {
                return FALSE; // Don't spawn on slippery surfaces
            }
        }

        for (i = 0; i < 30; i++) {
            if (floor->type == sSafeFloorsGeneral[i])
                return TRUE; // Check if surface type is valid
        }
    }
    return FALSE;
}

static u8 is_in_avoidance_point(Vec3s pos, struct AreaParams *areaParams)
{
    struct AvoidancePoint *avoidancePoint;
    Vec3s avoidancePos;
    u32 i;

    if (areaParams->numAvoidancePoints > 0) {
        for (i = 0; i < areaParams->numAvoidancePoints; i++) {
            avoidancePoint = &(*areaParams->avoidancePoints)[i];

            vec3s_copy(avoidancePos, avoidancePoint->pos);

            if ((sqrtf(sqr(pos[0] - avoidancePos[0]) + sqr(pos[2] - avoidancePos[2]))
                 < avoidancePoint->radius)
                && (ABSF(pos[1] - avoidancePos[1]) < avoidancePoint->height))
                return TRUE;
        }
    }
    return FALSE;
}

void get_safe_position(struct Object *obj, Vec3s pos, f32 minHeightRange, f32 maxHeightRange, u16 *seed,
                       u8 floorSafeLevel, u8 randPosFlags) {
    f32 minX, maxX, minY, maxY, minZ, maxZ, minHeight, maxHeight, waterLevel, lowFloorHeight, cHeight,
        highFloorHeight;
    u32 objCanBeUnderwater;
    u8 killOnOob = FALSE;
    struct Surface *lowFloor, *ceil, *highFloor;
    struct AreaParams *areaParams;

    s32 tries = 0;

    if (gCurrLevelNum < 4 || sLevelParams[gCurrLevelNum - 4] == NULL) {
        pos[0] = 0;
        pos[1] = 5000;
        pos[2] = 0;
        return;
    }

    // Collision queries only see the currently loaded area's surfaces, so
    // the bounds must be that area's too.
    sAreaIndex = gCurrAreaIndex;
    areaParams = &(*sLevelParams[gCurrLevelNum - 4])[sAreaIndex - 1];

    // The search must not disturb gameplay RNG.
    genrand_push();
    init_genrand(*seed);

    // Apply this area's wall-check search distance.
    sWallCheckRaycasterSearchDist = areaParams->wallCheckRaycasterSearchDist;
    sWallCheckNumRaySteps = sWallCheckRaycasterSearchDist / WALL_CHECK_RAYCASTER_STEP_SIZE;

    // Kill wall rays on OoB for these courses
    if ((gCurrCourseNum == COURSE_JRB) || (gCurrCourseNum == COURSE_BBH)
        || (gCurrCourseNum == COURSE_DDD) || (gCurrCourseNum == COURSE_WDW)) {
        killOnOob = TRUE;
    }

    minX = areaParams->minX;
    maxX = areaParams->maxX;
    minY = areaParams->minY;
    maxY = areaParams->maxY;
    minZ = areaParams->minZ;
    maxZ = areaParams->maxZ;

    // Handle special cases for bounds
    if ((gCurrCourseNum == COURSE_THI) && (sAreaIndex == 3)) {
        if (randPosFlags & RAND_POSITION_FLAG_THI_A3_ABOVE_MESH)
            minY = 2200;
        else
            maxY = 1750;
    } else if ((gCurrCourseNum == COURSE_PSS)) {
        if (randPosFlags & RAND_POSITION_FLAG_SPAWN_TOP_OF_SLIDE) {
            minY = 6100;
            minX = 3100;
        } else if (randPosFlags & RAND_POSITION_FLAG_SPAWN_BOTTOM_OF_SLIDE) {
            maxY = -3500;
            minZ = 4000;
        } else
            minY = -1000;

    } else if ((gCurrCourseNum == COURSE_CCM) && (sAreaIndex == 2)) {
        if (randPosFlags & RAND_POSITION_FLAG_SPAWN_TOP_OF_SLIDE) {
            minY = 6600;
            maxX = -4800;
        } else if (randPosFlags & RAND_POSITION_FLAG_SPAWN_BOTTOM_OF_SLIDE) {
            maxY = -3900;
            maxZ = -6400;
        }
    }

    while (TRUE) {
        // Never freeze the console: after enough failed tries, give up and
        // park the object high over the level origin.
        if (++tries > 20000) {
            pos[0] = 0;
            pos[1] = 5000;
            pos[2] = 0;
            genrand_pop();
            return;
        }

        // Generate random position
        pos[0] = get_val_in_range_uniform(minX, maxX);
        pos[1] = get_val_in_range_uniform(minY, maxY);
        pos[2] = get_val_in_range_uniform(minZ, maxZ);

        lowFloorHeight = find_floor(pos[0], pos[1] + 20, pos[2], &lowFloor);

        if (lowFloor == NULL)
            continue;

        if ((pos[1] - lowFloorHeight) > (gCurrCourseNum == COURSE_BBH ? 350 : 800))
            continue;

        if (lowFloorHeight + 20 <= maxY) {
            pos[1] = lowFloorHeight + 20;
        }

        // Move out of any walls. This has to be done here because otherwise
        // there's the possibility of being pushed out of the wall into OoB or a ceiling
        vec3s_resolve_wall_collisions(
            pos, (randPosFlags & RAND_POSITION_FLAG_SPAWN_FAR_FROM_WALLS) ? 500.0f : 50.0f);

        lowFloorHeight = find_floor(pos[0], pos[1], pos[2], &lowFloor);

        if (lowFloor == NULL)
            continue;

        if ((pos[1] - lowFloorHeight) > (gCurrCourseNum == COURSE_BBH ? 350 : 800))
            continue;

        pos[1] = lowFloorHeight;

        if (!is_floor_safe(lowFloor, floorSafeLevel, randPosFlags))
            continue;

        if ((randPosFlags & RAND_POSITION_FLAG_SAFE) && (lowFloor->normal.y < 0.9))
            continue;

        // Snap to ground and check if safe
        objCanBeUnderwater =
            (randPosFlags
                 & (RAND_POSITION_FLAG_CAN_BE_UNDERWATER | RAND_POSITION_FLAG_MUST_BE_UNDERWATER)
             || (areaParams->areaParamFlags & AREA_PARAM_FLAG_CHANGING_WATER_LEVEL));
        waterLevel = find_water_level(pos[0], pos[2]);
        minHeight = pos[1] + minHeightRange;
        maxHeight = pos[1] + maxHeightRange;

        // Let objects spawn anywhere in water
        if (floorSafeLevel != FLOOR_SAFE_GROUNDED
            || (randPosFlags & RAND_POSITION_FLAG_MUST_BE_UNDERWATER)) {
            if ((objCanBeUnderwater && (waterLevel > maxHeight)
                 && !(areaParams->areaParamFlags & AREA_PARAM_FLAG_CHANGING_WATER_LEVEL))
                || (randPosFlags & RAND_POSITION_FLAG_MUST_BE_UNDERWATER))
                maxHeight = waterLevel;
        }

        // Prevent objects from spawning too high above water in BBH
        if ((gCurrCourseNum == COURSE_BBH) && (pos[1] < waterLevel) && (maxHeight > waterLevel))
            maxHeight = waterLevel + 100.f;

        // Check if max height has gone above the level bounds
        if (maxHeight > maxY)
            maxHeight = maxY;

        pos[1] = get_val_in_range_uniform(minHeight, maxHeight);

        // Start checking if position is valid//

        // Ceiling check
        cHeight = find_ceil(pos[0], lowFloorHeight + 80, pos[2], &ceil);

        if (pos[1] > cHeight - 100.f) // If in a ceiling, cancel spawn
            continue;

        // Floor Check
        highFloorHeight = find_floor(pos[0], cHeight - 80, pos[2],
                                     &highFloor); // Find floor under object assuming 80 units of space

        if ((highFloorHeight > (pos[1] + 20))
            && ((highFloorHeight - pos[1])
                < 1500)) // If under floor and not large distance, deny height
            continue;

        if (!objCanBeUnderwater && (waterLevel > pos[1]))
            continue;

        if ((randPosFlags & RAND_POSITION_FLAG_MUST_BE_UNDERWATER) && (waterLevel < pos[1]))
            continue;

        if (randPosFlags & RAND_POSITION_FLAG_BBH_HMC_LIMITED_ROOMS) {
            if ((gCurrCourseNum == COURSE_BBH) && (lowFloor->room == 9)) {
                continue;
            } else if ((gCurrCourseNum == COURSE_HMC) && (lowFloor->room == 8)) {
                continue;
            }
        }

        // Check the avoidance points against the final position. (Upstream
        // originally checked them before the height was finalized, which let
        // spawns drift into avoided volumes; they fixed it in Oct 2022.)
        if (is_in_avoidance_point(pos, areaParams))
            continue;

        // Wall Check
        if (!is_safe_near_walls(pos, killOnOob))
            continue;

        genrand_pop();
        return;
    }
}
