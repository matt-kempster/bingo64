#include <ultra64.h>

#include "sm64.h"
#include "splatoon.h"
#include "game.h"
#include "area.h"
#include "level_update.h"
#include "memory.h"
#include "engine/math_util.h"
#include "engine/graph_node.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"

// Splatoon mode. When Mario stands on a static floor triangle, the
// level's own rendered geometry gets recolored: we walk the area's
// display lists and tint every vertex that sits inside that collision
// triangle's footprint. The paint costs no draw calls and no buffers —
// the level itself is the ink. It naturally washes off when the level
// data reloads.

#define SPLATOON_MAX_TRIS 1000

// F3DEX display list opcodes (from PR/gbi.h, where G_ENDDL is defined
// relative to G_IMMFIRST so we name the raw bytes here).
#define OP_VTX 0x04
#define OP_DL 0x06
#define OP_MOVEMEM 0x03
#define OP_ENDDL 0xB8

// Marks a vertex whose normal has been converted to a baked color.
// The flag field is unused padding in every SM64 vertex.
#define BAKED_FLAG 0x5A17

// Ink color, overwrites the terrain's baked vertex colors.
#define INK_R 0xFF
#define INK_G 0x40
#define INK_B 0xA0

// The visual mesh does not always line up with the collision mesh, so
// besides tinting the stepped-on triangle's footprint we also splat all
// vertices near Mario's feet. Radius chosen to look like an ink blob.
#define SPLAT_RADIUS 130.0f
#define SPLAT_Y_WINDOW 180.0f
#define SPLAT_MOVE_MIN 45.0f

enum SplatoonWalkOp { WALK_BAKE, WALK_TINT, WALK_SPLAT };
static f32 sSplatX, sSplatY, sSplatZ;

s32 gSplatoonEnabled = 0;
s32 gSplatoonPaintedCount = 0;
s32 gSplatoonTotalFloors = 0;

static struct Surface *sPaintedSurfs[SPLATOON_MAX_TRIS];
static struct Surface *sLastFloor = NULL;
static s32 sBaked = 0;
static f32 sLastSplat[3];
static s32 sHasSplatted = 0;

// Current material lights, tracked while walking display lists.
static u8 sLightCol[3];
static s8 sLightDir[3];
static u8 sAmbCol[3];

// Each level display list gets wrapped so it renders with lighting off
// (baked colors) without disturbing objects rendered after it.
#define MAX_TRAMPOLINES 64
static Gfx sTrampolines[MAX_TRAMPOLINES][4];
static s32 sTrampolineCount = 0;

void splatoon_clear(void) {
    gSplatoonPaintedCount = 0;
    gSplatoonTotalFloors = 0;
    sLastFloor = NULL;
    sBaked = 0;
    sTrampolineCount = 0;
    sHasSplatted = 0;
}

static s32 splatoon_count_floors(void) {
    struct SurfaceNode *node;
    s32 i, j;
    s32 count = 0;

    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
            node = gStaticSurfacePartition[i][j][SPATIAL_PARTITION_FLOORS].next;
            while (node != NULL) {
                count++;
                node = node->next;
            }
        }
    }
    return count;
}

// Turns one lit vertex into a plain colored vertex: computes the same
// ambient + diffuse * dot(normal, light) shade the RSP would, and
// stores it as a baked color. After this the vertex is paintable.
static void bake_vtx_array(Vtx *v, s32 n) {
    s32 i, c;
    f32 nx, ny, nz, lx, ly, lz, nlen, llen, dot;

    lx = sLightDir[0];
    ly = sLightDir[1];
    lz = sLightDir[2];
    llen = sqrtf(lx * lx + ly * ly + lz * lz);
    if (llen < 0.001f) {
        llen = 1.0f;
    }

    for (i = 0; i < n; i++) {
        if (v[i].v.flag == BAKED_FLAG) {
            continue;
        }
        nx = (s8) v[i].v.cn[0];
        ny = (s8) v[i].v.cn[1];
        nz = (s8) v[i].v.cn[2];
        nlen = sqrtf(nx * nx + ny * ny + nz * nz);
        if (nlen < 0.001f) {
            nlen = 1.0f;
        }
        dot = (nx * lx + ny * ly + nz * lz) / (nlen * llen);
        if (dot < 0.0f) {
            dot = 0.0f;
        }
        for (c = 0; c < 3; c++) {
            s32 col = sAmbCol[c] + (s32)(sLightCol[c] * dot);
            v[i].v.cn[c] = col > 255 ? 255 : col;
        }
        v[i].v.flag = BAKED_FLAG;
    }
}

// Tints every vertex in the array that lies on the surface's plane and
// inside its triangle when seen from above. The small margins mean the
// triangle's own corner vertices (which sit exactly on the edges) are
// included, so shared vertices blend the ink into neighboring
// triangles like soft ink edges.
static void tint_vtx_array(Vtx *v, s32 n, struct Surface *surf) {
    f32 x1 = surf->vertex1[0], z1 = surf->vertex1[2];
    f32 x2 = surf->vertex2[0], z2 = surf->vertex2[2];
    f32 x3 = surf->vertex3[0], z3 = surf->vertex3[2];
    f32 denom = (z2 - z3) * (x1 - x3) + (x3 - x2) * (z1 - z3);
    f32 x, y, z, planeDist, a, b, c;
    s32 i;

    if (denom > -0.001f && denom < 0.001f) {
        return;
    }

    for (i = 0; i < n; i++) {
        x = v[i].v.ob[0];
        y = v[i].v.ob[1];
        z = v[i].v.ob[2];

        planeDist = surf->normal.x * x + surf->normal.y * y + surf->normal.z * z
                    + surf->originOffset;
        if (planeDist < -8.0f || planeDist > 8.0f) {
            continue;
        }

        a = ((z2 - z3) * (x - x3) + (x3 - x2) * (z - z3)) / denom;
        b = ((z3 - z1) * (x - x3) + (x1 - x3) * (z - z3)) / denom;
        c = 1.0f - a - b;
        if (a < -0.01f || b < -0.01f || c < -0.01f) {
            continue;
        }

        if (v[i].v.flag != BAKED_FLAG) {
            continue; // Still a lit vertex; recoloring would do nothing.
        }
        v[i].v.cn[0] = INK_R;
        v[i].v.cn[1] = INK_G;
        v[i].v.cn[2] = INK_B;
    }
}

// Inks every baked vertex inside a cylinder around Mario's feet. This
// covers spots where the visual mesh is coarser than the collision
// mesh and the footprint test would miss every vertex.
static void splat_vtx_array(Vtx *v, s32 n) {
    f32 dx, dy, dz;
    s32 i;

    for (i = 0; i < n; i++) {
        if (v[i].v.flag != BAKED_FLAG) {
            continue;
        }
        dx = v[i].v.ob[0] - sSplatX;
        dy = v[i].v.ob[1] - sSplatY;
        dz = v[i].v.ob[2] - sSplatZ;
        if (dy < -SPLAT_Y_WINDOW || dy > SPLAT_Y_WINDOW) {
            continue;
        }
        if (dx * dx + dz * dz > SPLAT_RADIUS * SPLAT_RADIUS) {
            continue;
        }
        v[i].v.cn[0] = INK_R;
        v[i].v.cn[1] = INK_G;
        v[i].v.cn[2] = INK_B;
    }
}

// Walks a display list, applying `op` to every vertex array it loads.
// The bake pass also tracks each material's lights along the way.
static void walk_display_list(Gfx *dl, s32 op, struct Surface *surf, s32 depth) {
    u32 w0, w1;

    if (dl == NULL || depth > 8) {
        return;
    }
    for (;;) {
        w0 = dl->words.w0;
        w1 = dl->words.w1;

        if ((w0 >> 24) == OP_ENDDL) {
            return;
        }
        if ((w0 >> 24) == OP_DL) {
            Gfx *sub = segmented_to_virtual((void *) w1);
            if (((w0 >> 16) & 0xFF) == G_DL_PUSH) {
                walk_display_list(sub, op, surf, depth + 1);
            } else {
                dl = sub;
                continue;
            }
        } else if ((w0 >> 24) == OP_VTX) {
            s32 n = (w0 >> 10) & 0x3F;
            Vtx *v = segmented_to_virtual((void *) w1);
            if (op == WALK_TINT) {
                tint_vtx_array(v, n, surf);
            } else if (op == WALK_SPLAT) {
                splat_vtx_array(v, n);
            } else {
                bake_vtx_array(v, n);
            }
        } else if ((w0 >> 24) == OP_MOVEMEM && op == WALK_BAKE) {
            u8 param = (w0 >> 16) & 0xFF;
            if (param == G_MV_L0) {
                u8 *light = segmented_to_virtual((void *) w1);
                sLightCol[0] = light[0];
                sLightCol[1] = light[1];
                sLightCol[2] = light[2];
                sLightDir[0] = (s8) light[8];
                sLightDir[1] = (s8) light[9];
                sLightDir[2] = (s8) light[10];
            } else if (param == G_MV_L1) {
                u8 *amb = segmented_to_virtual((void *) w1);
                sAmbCol[0] = amb[0];
                sAmbCol[1] = amb[1];
                sAmbCol[2] = amb[2];
            }
        }
        dl++;
    }
}

// Finds every display list in the area's geo graph (skipping objects —
// only the level's own geometry gets painted).
static void walk_graph_node(struct GraphNode *firstNode, s32 op, struct Surface *surf, s32 depth) {
    struct GraphNode *node = firstNode;

    if (firstNode == NULL || depth > 16) {
        return;
    }
    do {
        if (node->type == GRAPH_NODE_TYPE_DISPLAY_LIST) {
            struct GraphNodeDisplayList *dlNode = (struct GraphNodeDisplayList *) node;
            void *dl = dlNode->displayList;
            s32 wrapped = dl >= (void *) sTrampolines
                          && dl < (void *) (sTrampolines + MAX_TRAMPOLINES);

            if (wrapped) {
                // Already wrapped; the real list is inside the trampoline.
                dl = (void *) ((Gfx *) dl)[1].words.w1;
            }
            if (dl != NULL) {
                walk_display_list(segmented_to_virtual(dl), op, surf, 0);

                if (op == WALK_BAKE && !wrapped && sTrampolineCount < MAX_TRAMPOLINES) {
                    Gfx *g = sTrampolines[sTrampolineCount++];
                    gSPClearGeometryMode(g++, G_LIGHTING);
                    gSPDisplayList(g++, dl);
                    gSPSetGeometryMode(g++, G_LIGHTING);
                    gSPEndDisplayList(g);
                    dlNode->displayList = sTrampolines[sTrampolineCount - 1];
                }
            }
        }
        if (node->type != GRAPH_NODE_TYPE_OBJECT_PARENT && node->children != NULL) {
            walk_graph_node(node->children, op, surf, depth + 1);
        }
        node = node->next;
    } while (node != firstNode);
}

static void splatoon_step(struct Surface *floor) {
    s32 i;

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

    sPaintedSurfs[gSplatoonPaintedCount++] = floor;

    if (gCurrentArea != NULL && gCurrentArea->unk04 != NULL) {
        walk_graph_node((struct GraphNode *) gCurrentArea->unk04, WALK_TINT, floor, 0);
    }
}

void splatoon_update(void) {
    if (!gSplatoonEnabled || gMarioState->marioObj == NULL || gCurrentArea == NULL) {
        return;
    }

    // First frame in this area with splatoon on: bake the level's
    // lighting into its vertices so they become paintable.
    if (!sBaked && gCurrentArea->unk04 != NULL) {
        sLightCol[0] = sLightCol[1] = sLightCol[2] = 255;
        sAmbCol[0] = sAmbCol[1] = sAmbCol[2] = 127;
        sLightDir[0] = sLightDir[1] = sLightDir[2] = 40;
        walk_graph_node((struct GraphNode *) gCurrentArea->unk04, WALK_BAKE, NULL, 0);
        sBaked = 1;
    }

    if (!(gMarioState->action & ACT_FLAG_AIR)) {
        f32 dx, dz;

        splatoon_step(gMarioState->floor);

        dx = gMarioState->pos[0] - sLastSplat[0];
        dz = gMarioState->pos[2] - sLastSplat[2];
        if (!sHasSplatted || dx * dx + dz * dz > SPLAT_MOVE_MIN * SPLAT_MOVE_MIN) {
            sHasSplatted = 1;
            sLastSplat[0] = gMarioState->pos[0];
            sLastSplat[1] = gMarioState->pos[1];
            sLastSplat[2] = gMarioState->pos[2];
            sSplatX = gMarioState->pos[0];
            sSplatY = gMarioState->pos[1];
            sSplatZ = gMarioState->pos[2];
            walk_graph_node((struct GraphNode *) gCurrentArea->unk04, WALK_SPLAT, NULL, 0);
        }
    }
}
