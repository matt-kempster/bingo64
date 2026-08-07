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

// Opaque stand-ins for N64 graphics types that only appear behind pointers.
typedef long Vp;
typedef long Gfx;
typedef long Mtx;
typedef long LookAt;

struct Object;

#endif
