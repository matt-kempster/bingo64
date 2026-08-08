#include <ultra64.h>

#include "sm64.h"
#include "splatoon.h"
#include "game.h"
#include "area.h"
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

#define SPLATOON_MAX_TRIS 1000

// Swimming should only paint when Mario hugs the bottom, not the whole
// lakebed under his swim path.
#define SPLATOON_MAX_HEIGHT_ABOVE_FLOOR 100.0f

extern Mat4 gMatStack[32];
extern struct Surface *sSurfacePool;

s32 gSplatoonEnabled = 0;
s32 gSplatoonPaintedCount = 0;
s32 gSplatoonTotalFloors = 0;

static struct Surface *sPaintedSurfs[SPLATOON_MAX_TRIS];
static struct Surface *sLastFloor = NULL;

// Painted triangles live in these persistent buffers, not the per-frame
// display list pool, so a well-painted level costs no pool memory.
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

void splatoon_clear(void) {
    gSplatoonPaintedCount = 0;
    gSplatoonTotalFloors = 0;
    sLastFloor = NULL;
    {
        Gfx *g = sPaintGfx;
        gSPEndDisplayList(g);
    }
}

static s32 splatoon_count_floors(void) {
    s32 i;
    s32 count = 0;

    // Walk the surface pool, not the cell partition: a triangle spanning
    // several cells has a node in each and would be counted repeatedly.
    for (i = 0; i < gNumStaticSurfaces; i++) {
        if (sSurfacePool[i].normal.y > 0.01) {
            count++;
        }
    }
    return count;
}

static void splatoon_step(struct Surface *floor) {
    s32 i;
    Vtx *v;
    Gfx *g;

    if (floor == NULL || floor->object != NULL || floor == sLastFloor) {
        return;
    }
    sLastFloor = floor;

    for (i = 0; i < gSplatoonPaintedCount; i++) {
        if (sPaintedSurfs[i] == floor) {
            return;
        }
    }
    if (gSplatoonPaintedCount >= SPLATOON_MAX_TRIS) {
        return;
    }
    if (gSplatoonTotalFloors == 0) {
        gSplatoonTotalFloors = splatoon_count_floors();
    }

    i = gSplatoonPaintedCount++;
    sPaintedSurfs[i] = floor;

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

    if (gSplatoonPaintedCount == 0) {
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
