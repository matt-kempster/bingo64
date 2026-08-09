#include <ultra64.h>

#include "sm64.h"
#include "splatoon.h"
#include "game_init.h"
#include "area.h"
#include "bingo.h"
#include "level_update.h"
#include "memory.h"
#include "geo_misc.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "object_list_processor.h"

// Splatoon mode. Every static floor triangle Mario stands on gets
// remembered and drawn as translucent ink from then on. The rendering
// approach (decal render mode, camera matrix from gMatStack[1]) comes
// from the original `splatoon` branch's triangle-highlight experiments.

// Big enough for every course area's floor count (Hazy Maze Cave is
// the largest at 748).
#define SPLATOON_MAX_TRIS 1200

// Swimming should only paint when Mario hugs the bottom, not the whole
// lakebed under his swim path.
#define SPLATOON_MAX_HEIGHT_ABOVE_FLOOR 100.0f

// Painted floors are remembered as (area, surface index) bits. Static
// surfaces load in a deterministic order, so a surface's pool index is a
// stable id for it across area reloads (e.g. the WDW downtown pipe) —
// that is what lets paint survive area transitions and keeps the painted
// count free of double counting. SURFACE_POOL_SIZE must match
// sSurfacePoolSize in surface_load.c.
#define SPLATOON_MAX_AREAS 9  // area indices used by levels are 1..8
#define SURFACE_POOL_SIZE 2300

extern Mat4 gMatStack[32];
extern struct Surface *sSurfacePool;

s32 gSplatoonEnabled = 0;
s32 gSplatoonPaintedCount = 0;  // unique floors painted, all areas of the course
s32 gSplatoonTotalFloors = 0;   // static floors in the current area

static u8 sPaintedBits[SPLATOON_MAX_AREAS][(SURFACE_POOL_SIZE + 7) / 8];
static s32 sAreaIndex = 0;
static struct Surface *sLastFloor = NULL;

// Painted triangles live in these persistent buffers, not the per-frame
// display list pool, so a well-painted level costs no pool memory. They
// hold the current area's decals; sDecalCount can be below the painted
// count when the paint is spread over several areas.
static s32 sDecalCount = 0;
static Vtx sPaintVerts[SPLATOON_MAX_TRIS * 3];
static Gfx sPaintGfx[SPLATOON_MAX_TRIS * 2 + 1];

static const Gfx dl_splatoon_begin[] = {
    gsDPPipeSync(),
    gsDPSetRenderMode(G_RM_ZB_XLU_DECAL, G_RM_NOOP2),
    gsSPClearGeometryMode(G_LIGHTING),
    gsSPSetGeometryMode(G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH),
    gsSPTexture(0, 0, 0, 0, G_OFF),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPEndDisplayList(),
};

static void splatoon_reset_decals(void) {
    Gfx *g = sPaintGfx;
    sDecalCount = 0;
    sLastFloor = NULL;
    gSPEndDisplayList(g);
}

/**
 * Forgets all paint. Runs when the course changes (from
 * disable_bingo_modifiers), so paint persists across areas of one course
 * but never follows Mario out of it.
 */
void splatoon_clear(void) {
    s32 i, j;

    gSplatoonPaintedCount = 0;
    gSplatoonTotalFloors = 0;
    splatoon_reset_decals();
    for (i = 0; i < SPLATOON_MAX_AREAS; i++) {
        for (j = 0; j < (SURFACE_POOL_SIZE + 7) / 8; j++) {
            sPaintedBits[i][j] = 0;
        }
    }
}

static void splatoon_add_decal(struct Surface *floor) {
    s32 i;
    Vtx *v;
    Gfx *g;

    if (sDecalCount >= SPLATOON_MAX_TRIS) {
        return;
    }
    i = sDecalCount++;

    v = &sPaintVerts[i * 3];
    make_vertex(v, 0, floor->vertex1[0], floor->vertex1[1], floor->vertex1[2], 0, 0,
                0xFF, 0x40, 0xA0, 0xB4);
    make_vertex(v, 1, floor->vertex2[0], floor->vertex2[1], floor->vertex2[2], 0, 0,
                0xFF, 0x40, 0xA0, 0xB4);
    make_vertex(v, 2, floor->vertex3[0], floor->vertex3[1], floor->vertex3[2], 0, 0,
                0xE0, 0x30, 0x90, 0xB4);

    g = &sPaintGfx[i * 2];
    gSPVertex(g++, VIRTUAL_TO_PHYSICAL(v), 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g);
}

/**
 * Called from load_area_terrain once the area's static surfaces are in.
 * Surface pointers from before the load are dead, so the decal buffer is
 * rebuilt from the painted-floor bits for this area.
 */
void splatoon_on_area_load(s16 areaIndex) {
    s32 idx;
    struct Surface *surf;

    sAreaIndex = (areaIndex >= 0 && areaIndex < SPLATOON_MAX_AREAS) ? areaIndex : 0;
    splatoon_reset_decals();

    gSplatoonTotalFloors = 0;
    for (idx = 0; idx < gNumStaticSurfaces; idx++) {
        surf = &sSurfacePool[idx];
        if (surf->normal.y > 0.01) {
            gSplatoonTotalFloors++;
            if (sPaintedBits[sAreaIndex][idx >> 3] & (1 << (idx & 7))) {
                splatoon_add_decal(surf);
            }
        }
    }
}

static void splatoon_step(struct Surface *floor) {
    s32 idx;

    if (floor == NULL || floor->object != NULL || floor == sLastFloor) {
        return;
    }
    sLastFloor = floor;

    idx = floor - sSurfacePool;
    if (idx < 0 || idx >= gNumStaticSurfaces) {
        return;
    }
    if (sPaintedBits[sAreaIndex][idx >> 3] & (1 << (idx & 7))) {
        return;
    }
    sPaintedBits[sAreaIndex][idx >> 3] |= (1 << (idx & 7));
    gSplatoonPaintedCount++;
    splatoon_add_decal(floor);
    bingo_update(BINGO_UPDATE_SPLATOON_PAINTED);
}

// Called from render_game() right after the world is drawn, while the
// z-buffer is still valid (the ink is a decal on top of the floor).
void splatoon_render(void) {
    Mtx *mtx;

    if (!gSplatoonEnabled || gMarioState->marioObj == NULL) {
        return;
    }

    if (!(gMarioState->action & ACT_FLAG_AIR)
        && gMarioState->pos[1] - gMarioState->floorHeight < SPLATOON_MAX_HEIGHT_ABOVE_FLOOR) {
        splatoon_step(gMarioState->floor);
    }

    if (sDecalCount == 0) {
        return;
    }

    mtx = alloc_display_list(sizeof(Mtx));
    if (mtx == NULL) {
        return;
    }
    mtxf_to_mtx(mtx, gMatStack[1]);

    gSPDisplayList(gDisplayListHead++, dl_splatoon_begin);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx),
              G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPDisplayList(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(sPaintGfx));
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}
