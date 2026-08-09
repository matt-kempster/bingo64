#pragma once

/// Enables extended ram on N64.
/// Bingo64 keeps this off: the ROM must run on a stock 4MB console, and the
/// dynamic pool (BSS end to RAM end) still exceeds the old expanded pool.
// #define N64_USE_EXTENDED_RAM

/// Enables crash screen on N64
#define N64_CRASH_SCREEN
