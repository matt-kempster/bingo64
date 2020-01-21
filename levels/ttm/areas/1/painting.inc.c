#include "game/paintings.h"

// 0x070122F0 - 0x07012308
static const Lights1 ttm_seg7_lights_070122F0 = gdSPDefLights1(
    0x50, 0x50, 0x50,
    0xff, 0xff, 0xff, 0x32, 0x32, 0x32
);

// 0x07012308 - 0x07012388
static const Vtx ttm_seg7_vertex_07012308[] = {
    {{{     0,      0,      0}, 0, {   -32,    992}, {0x00, 0x00, 0x7f, 0xff}}},
    {{{   614,      0,      0}, 0, {  2012,    992}, {0x00, 0x00, 0x7f, 0xff}}},
    {{{   614,    307,      0}, 0, {  2012,      0}, {0x00, 0x00, 0x7f, 0xff}}},
    {{{     0,    307,      0}, 0, {   -32,      0}, {0x00, 0x00, 0x7f, 0xff}}},
    {{{     0,    307,      0}, 0, {   -32,    992}, {0x00, 0x00, 0x7f, 0xff}}},
    {{{   614,    307,      0}, 0, {  2012,    992}, {0x00, 0x00, 0x7f, 0xff}}},
    {{{   614,    614,      0}, 0, {  2012,    -28}, {0x00, 0x00, 0x7f, 0xff}}},
    {{{     0,    614,      0}, 0, {   -32,    -28}, {0x00, 0x00, 0x7f, 0xff}}},
};

// 0x07012388 - 0x070123A0
const Gfx ttm_seg7_dl_07012388[] = {
    gsSP2Triangles( 0,  1,  2, 0x0,  0,  2,  3, 0x0),
    gsSPEndDisplayList(),
};

// 0x070123A0 - 0x070123B8
const Gfx ttm_seg7_dl_070123A0[] = {
    gsSP2Triangles( 4,  5,  6, 0x0,  4,  6,  7, 0x0),
    gsSPEndDisplayList(),
};

// 0x070123B8 - 0x07012410
const Gfx ttm_seg7_dl_070123B8[] = {
    gsDPPipeSync(),
    gsSPSetGeometryMode(G_LIGHTING | G_SHADING_SMOOTH),
    gsDPSetCombineMode(G_CC_MODULATERGB, G_CC_MODULATERGB),
    gsSPLight(&ttm_seg7_lights_070122F0.l, 1),
    gsSPLight(&ttm_seg7_lights_070122F0.a, 2),
    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD),
    gsDPTileSync(),
    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, 5, G_TX_NOLOD, G_TX_CLAMP, 6, G_TX_NOLOD),
    gsDPSetTileSize(0, 0, 0, (64 - 1) << G_TEXTURE_IMAGE_FRAC, (32 - 1) << G_TEXTURE_IMAGE_FRAC),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsSPEndDisplayList(),
};

// 0x07012410 - 0x07012430
const Gfx ttm_seg7_dl_07012410[] = {
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPEndDisplayList(),
};

// 0x07012430 - 0x07012450
static const Gfx ttm_seg7_painting_dl_07012430[] = {
    gsDPTileSync(),
    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, 5, G_TX_NOLOD, G_TX_CLAMP, 6, G_TX_NOLOD),
    gsDPSetTileSize(0, 0, 0, (64 - 1) << G_TEXTURE_IMAGE_FRAC, (32 - 1) << G_TEXTURE_IMAGE_FRAC),
    gsSPEndDisplayList(),
};

// 0x07012450 - 0x0701296A
static const PaintingData ttm_seg7_painting_points_07012450[] = {
    85,
      49, 2016,  889,
      53, 2016,  685,
      55, 1843,  787,
      50, 2016,  992,
      51, 1843,  992,
      52, 1843,  583,
      75, 2016,  513,
      54, 1671,  889,
      59, 1671,  685,
      62, 1502,  787,
      56, 1502,  992,
      57, 1671,  992,
      58, 1502,  583,
      60, 1671,  513,
      61, 1330,  889,
      65, 1330,  685,
      63, 1162,  992,
      64, 1330,  992,
      66, 1162,  583,
      67, 1330,  513,
      69, 1162,  787,
      68,  989,  889,
      70,  821,  992,
      71,  989,  992,
      73,  989,  685,
      72,  821,  583,
      74,  989,  513,
      77, 2016,  308,
      78, 1843,  410,
      76, 1843,  204,
      81, 1502,  410,
      80, 1671,  308,
      47, 1671,  102,
      79, 1502,  204,
      46, 1330,  102,
      82, 1162,  204,
      83, 1330,  308,
      84, 1162,  410,
      86,  989,  308,
      85,  821,  204,
      48,  989,  102,
      25, 1502,    0,
      31, 1162,    0,
      19, 1843,    0,
      37,  821,    0,
     120,  821,  787,
     119,  649,  889,
     122,  481,  992,
     121,  649,  992,
     124,  649,  685,
     125,  481,  583,
     123,  649,  513,
     127,  481,  787,
     126,  308,  889,
     129,  140,  992,
     128,  308,  992,
     132,  308,  513,
     131,  308,  685,
     130,  140,  583,
     134,  140,  787,
     133,  -32,  889,
     135,  -32,  513,
     136,  821,  410,
     116,  649,  102,
     137,  649,  308,
     114,  481,  204,
     138,  481,  410,
     139,  308,  308,
     118,  140,  204,
     115,  308,  102,
     140,  140,  410,
     117,  -32,  102,
      99,  481,    0,
     105,  140,    0,
     143, 2016,  102,
     145, 1330,    0,
     144, 1671,    0,
     142, 2016,    0,
     146,  989,    0,
     155,  -32,  685,
     156,  -32,  992,
     154,  -32,  308,
     151,  308,    0,
     150,  649,    0,
     153,  -32,    0,

// ttm_seg7_painting_triangles_07012650:
    132,
    13,  8,  5,
     0,  1,  2,
     3,  0,  4,
     4,  0,  2,
     5,  2,  1,
     1,  6,  5,
     7,  2,  8,
     5,  8,  2,
     2,  7,  4,
     7,  8,  9,
    10,  7,  9,
    11,  7, 10,
     7, 11,  4,
    12,  9,  8,
     8, 13, 12,
    21, 24, 45,
    14,  9, 15,
    12, 15,  9,
     9, 14, 10,
    16, 14, 20,
    17, 14, 16,
    14, 15, 20,
    14, 17, 10,
    15, 19, 18,
    18, 20, 15,
    19, 15, 12,
    20, 21, 16,
    18, 24, 20,
    21, 20, 24,
    22, 21, 45,
    23, 21, 22,
    21, 23, 16,
    24, 26, 25,
    25, 45, 24,
    26, 24, 18,
     6, 27, 28,
     5,  6, 28,
    29, 28, 27,
    27, 74, 29,
    29, 31, 28,
    13, 28, 31,
    28, 13,  5,
    36, 34, 35,
    12, 13, 30,
    13, 31, 30,
    31, 32, 33,
    32, 31, 29,
    33, 30, 31,
    33, 36, 30,
    30, 19, 12,
    19, 30, 36,
    18, 19, 37,
    19, 36, 37,
    34, 36, 33,
    35, 37, 36,
    37, 26, 18,
    35, 38, 37,
    26, 37, 38,
    25, 26, 62,
    26, 38, 62,
    38, 40, 39,
    39, 62, 38,
    40, 38, 35,
    41, 34, 33,
    33, 32, 41,
    42, 34, 75,
    34, 41, 75,
    35, 34, 42,
    32, 43, 76,
    41, 32, 76,
    43, 32, 29,
    29, 74, 43,
    43, 74, 77,
    46, 49, 52,
    42, 40, 35,
    39, 40, 44,
    40, 42, 78,
    44, 40, 78,
    25, 49, 45,
    45, 46, 22,
    46, 45, 49,
    47, 46, 52,
    48, 46, 47,
    46, 48, 22,
    58, 59, 57,
    49, 51, 50,
    50, 52, 49,
    51, 49, 25,
    50, 57, 52,
    52, 53, 47,
    53, 52, 57,
    53, 55, 47,
    54, 53, 59,
    55, 53, 54,
    53, 57, 59,
    56, 57, 50,
    57, 56, 58,
    58, 79, 59,
    59, 60, 54,
    60, 59, 79,
    60, 80, 54,
    61, 79, 58,
    62, 51, 25,
    39, 64, 62,
    51, 62, 64,
    50, 51, 66,
    51, 64, 66,
    63, 64, 39,
    64, 63, 65,
    65, 66, 64,
    66, 56, 50,
    56, 66, 67,
    65, 67, 66,
    58, 56, 70,
    56, 67, 70,
    67, 69, 68,
    68, 70, 67,
    69, 67, 65,
    70, 61, 58,
    68, 81, 70,
    61, 70, 81,
    71, 73, 84,
    71, 81, 68,
    72, 69, 65,
    65, 63, 72,
    68, 69, 73,
    69, 72, 82,
    73, 69, 82,
    44, 63, 39,
    63, 44, 83,
    72, 63, 83,
    73, 71, 68,
};


// 0x0701296C - 0x07012E84
static const PaintingData ttm_seg7_painting_points_0701296C[] = {
    85,
       0, 2016,   72,
       1, 2016,    0,
       2, 1843,    0,
       3, 1843,  174,
       4, 2016,  276,
       5, 1671,   72,
       6, 1671,    0,
       8,  989,   72,
       7,  989,    0,
      10,  821,    0,
       9, 1162,    0,
      11,  821,  174,
      12,  989,  276,
      13, 1162,  174,
      14, 1330,   72,
      15, 1502,    0,
      16, 1671,  276,
      17, 1502,  174,
      18, 1330,  276,
      19, 1843,  992,
      20, 2016,  889,
      22, 2016,  685,
      21, 1843,  583,
      23, 1843,  787,
      24, 1671,  889,
      25, 1502,  992,
      26, 1502,  583,
      27, 1671,  685,
      28, 1671,  481,
      30, 1502,  787,
      29, 1330,  889,
      31, 1162,  992,
      32, 1330,  481,
      33, 1162,  583,
      34, 1330,  685,
      35, 1162,  787,
      36,  989,  889,
      37,  821,  992,
      39,  821,  583,
      38,  989,  685,
      40,  989,  481,
      41, 2016,  481,
      42, 1843,  378,
      43, 1502,  378,
      44, 1162,  378,
      45,  821,  378,
      87,  649,   72,
      88,  -32,    0,
      90,  140,    0,
      89,  -32,   72,
      92,  308,   72,
      91,  140,  174,
      94,  481,  174,
      93,  649,  276,
      95,  481,    0,
      96,  308,  276,
      97,  821,  787,
      98,  649,  889,
      99,  481,  992,
     102,  649,  481,
     101,  649,  685,
     100,  481,  583,
     103,  481,  787,
     104,  308,  889,
     105,  140,  992,
     108,  308,  481,
     107,  308,  685,
     106,  140,  583,
     110,  -32,  889,
     109,  140,  787,
     111,  -32,  481,
     112,  481,  378,
     113,  140,  378,
     141, 1330,    0,
     142, 2016,  992,
     144, 1671,  992,
     145, 1330,  992,
     146,  989,  992,
     147,  649,    0,
     148,  -32,  276,
     149,  308,    0,
     150,  649,  992,
     151,  308,  992,
     152,  -32,  685,
     153,  -32,  992,

// ttm_seg7_painting_triangles_07012B6C:
    132,
    10,  7, 13,
     0,  1,  2,
     3,  0,  2,
     4,  0,  3,
     5,  2,  6,
     2,  5,  3,
     7,  8,  9,
     8,  7, 10,
    11,  7,  9,
    12,  7, 11,
     7, 12, 13,
    13, 14, 10,
    14, 73, 10,
     5,  6, 15,
     5, 16,  3,
    16,  5, 17,
    17,  5, 15,
    14, 15, 73,
    15, 14, 17,
    18, 14, 13,
    14, 18, 17,
    19, 74, 20,
    19, 20, 23,
    28, 27, 22,
    21, 41, 22,
    22, 23, 21,
    20, 21, 23,
    23, 24, 19,
    22, 27, 23,
    24, 23, 27,
    19, 24, 75,
    25, 75, 24,
    25, 24, 29,
    24, 27, 29,
    26, 29, 27,
    27, 28, 26,
    31, 36, 77,
    26, 34, 29,
    29, 30, 25,
    30, 29, 34,
    25, 30, 76,
    31, 76, 30,
    31, 30, 35,
    30, 34, 35,
    32, 34, 26,
    33, 35, 34,
    34, 32, 33,
    35, 36, 31,
    33, 39, 35,
    36, 35, 39,
    37, 36, 56,
    36, 39, 56,
    37, 77, 36,
    28, 16, 43,
    38, 56, 39,
    39, 40, 38,
    40, 39, 33,
    22, 41, 42,
    41,  4, 42,
     3, 42,  4,
    42, 28, 22,
    28, 42, 16,
     3, 16, 42,
    26, 28, 43,
    17, 43, 16,
    43, 32, 26,
    32, 43, 18,
    17, 18, 43,
    33, 32, 44,
    32, 18, 44,
    13, 44, 18,
    13, 12, 44,
    44, 40, 33,
    40, 44, 12,
    38, 40, 45,
    40, 12, 45,
    11, 45, 12,
     9, 46, 11,
    46,  9, 78,
    47, 49, 48,
    48, 49, 51,
    49, 79, 51,
    50, 80, 48,
    51, 50, 48,
    57, 56, 60,
    46, 53, 11,
    52, 46, 54,
    53, 46, 52,
    46, 78, 54,
    54, 50, 52,
    50, 54, 80,
    50, 55, 52,
    55, 50, 51,
    38, 60, 56,
    56, 57, 37,
    58, 57, 62,
    57, 60, 62,
    58, 81, 57,
    37, 57, 81,
    59, 60, 38,
    60, 59, 61,
    61, 62, 60,
    62, 63, 58,
    63, 62, 66,
    61, 66, 62,
    63, 66, 69,
    58, 63, 82,
    64, 82, 63,
    64, 63, 69,
    45, 59, 38,
    65, 66, 61,
    66, 65, 67,
    67, 69, 66,
    68, 69, 83,
    69, 68, 64,
    67, 83, 69,
    64, 68, 84,
    70, 83, 67,
    11, 53, 45,
    59, 45, 53,
    59, 53, 71,
    61, 59, 71,
    52, 71, 53,
    52, 55, 71,
    65, 71, 55,
    71, 65, 61,
    65, 55, 72,
    67, 65, 72,
    51, 72, 55,
    70, 72, 79,
    51, 79, 72,
    72, 70, 67,
};


// 0x07012E88
static const PaintingData *const ttm_seg7_painting_data_07012E88[] = {
    ttm_seg7_painting_points_07012450,
    ttm_seg7_painting_points_0701296C,
};

UNUSED static const u64 ttm_unused_0 = 0x0;


// 0x07012E98 - 0x07012EF8
static const Gfx ttm_seg7_painting_dl_07012E98[] = {
    gsSPDisplayList(ttm_seg7_dl_070123B8),
    gsSPVertex(ttm_seg7_vertex_07012308, 8, 0),
    gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, ttm_seg7_texture_07004000),
    gsDPLoadSync(),
    gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 64 * 32 - 1, CALC_DXT(64, G_IM_SIZ_16b_BYTES)),
    gsSPDisplayList(ttm_seg7_dl_07012388),
    gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, ttm_seg7_texture_07003000),
    gsDPLoadSync(),
    gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 64 * 32 - 1, CALC_DXT(64, G_IM_SIZ_16b_BYTES)),
    gsSPDisplayList(ttm_seg7_dl_070123A0),
    gsSPDisplayList(ttm_seg7_dl_07012410),
    gsSPEndDisplayList(),
};

// 0x07012EF8 - 0x07012F78
static const u8 *const ttm_seg7_painting_textures_07012EF8[] = {
    ttm_seg7_texture_07004000, ttm_seg7_texture_07003000,
};

// 0x07012F00 (PaintingData)
struct Painting ttm_slide_painting = {
    /* id */ 0x0000,
    /* Face Count */ 0x02,
    /* Ripple Shape */ RIPPLE_SHAPE_WAVE,
    /* Floor Status */ 0x00, 0x00, 0x00 /* which of the painting's nearby special floors Mario's on */,
    /* Ripple Status */ 0x00,
    /* Rotation */    0.0f,   90.0f,
    /* Position */ 3072.0f, 921.6f, -819.2f,
    /* Ripple Magnitude */    0.0f,   20.0f,  80.0f,
    1.0f, 0.9608f, 0.9524f,
    0.0f,   0.24f,   0.14f,
    0.0f,   40.0f,   30.0f,
    0.0f,
    0.0f,    0.0f,
    ttm_seg7_painting_dl_07012E98,
    ttm_seg7_painting_data_07012E88,
    ttm_seg7_painting_textures_07012EF8,
    64, 32,
    ttm_seg7_painting_dl_07012430,
    RIPPLE_TRIGGER_PROXIMITY, 0xFF, 0x00, 0x00, 0x00,
    460.8f,
};
