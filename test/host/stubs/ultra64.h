#ifndef HOST_STUB_ULTRA64_H
#define HOST_STUB_ULTRA64_H

// Host-test stand-in for the N64 SDK header. Just the basic types.

#include <stdint.h>
#include <stddef.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef unsigned long long int u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef signed long long int s64;
typedef float f32;
typedef double f64;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#endif
