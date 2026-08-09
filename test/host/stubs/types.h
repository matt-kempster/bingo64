#ifndef HOST_STUB_TYPES_H
#define HOST_STUB_TYPES_H

#include <ultra64.h>

typedef f32 Vec3f[3];
typedef s16 Vec3s[3];
typedef s32 Vec3i[3];
typedef f32 Vec4f[4];
typedef s16 Vec4s[4];
typedef f32 Mat4[4][4];
typedef uintptr_t BehaviorScript;
typedef s16 Collision;
typedef Collision TerrainData;
typedef TerrainData Vec3Terrain[3];
typedef s8 RoomData;
typedef u8 Texture;

// Opaque stand-ins for N64 graphics types that only appear behind pointers.
typedef long Vp;
typedef long Gfx;
typedef long Mtx;
typedef long LookAt;

struct Object;

// Refresh 16 headers use these macros from include/macros.h, which the host
// build does not pull in ahead of them.
#ifndef BAD_RETURN
#define BAD_RETURN(type) void
#endif
#ifndef UNUSED
#define UNUSED
#endif
#ifndef ALIGNED8
#define ALIGNED8
#endif

#endif
