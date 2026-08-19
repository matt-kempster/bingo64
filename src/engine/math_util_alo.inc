// The sm64ex-alo versions of math_util's trig/approach/spline core,
// preserved but never compiled (ALO is defined nowhere). The live
// versions in math_util.c are verbatim vanilla decomp per the
// vanilla-by-construction policy (2026-08-16). Differences from vanilla:
// approach_s32/f32 are dist-based rewrites (different overflow edges),
// anim_spline_poll multiplies by (1.0f/1000.0f) instead of dividing by
// 1000.0f (different f32 rounding), atan2 family is equivalent refactor.

s32 approach_s32(s32 current, s32 target, s32 inc, s32 dec) {
    s32 dist = (target - current);
    if (dist > 0) { // current < target
        current = ((dist >  inc) ? (current + inc) : target);
    } else if (dist < 0) { // current > target
        current = ((dist < -dec) ? (current - dec) : target);
    }
    return current;
}

f32 approach_f32(f32 current, f32 target, f32 inc, f32 dec) {
    f32 dist = (target - current);
    if (dist >= 0.0f) { // target >= current
        current = ((dist >  inc) ? (current + inc) : target);
    } else { // target < current
        current = ((dist < -dec) ? (current - dec) : target);
    }
    return current;
}

static u16 atan2_lookup(f32 y, f32 x) {
    return (x == 0)
        ? 0x0
        : atans(y / x);
}

/**
 * Compute the angle from (0, 0) to (x, y) as a s16. Given that terrain is in
 * the xz-plane, this is commonly called with (z, x) to get a yaw angle.
 */
s16 atan2s(f32 y, f32 x) {
    u16 ret;
    if (x >= 0) {
        if (y >= 0) {
            if (y >= x) {
                ret = atan2_lookup(x, y);
            } else {
                ret = 0x4000 - atan2_lookup(y, x);
            }
        } else {
            y = -y;
            if (y < x) {
                ret = 0x4000 + atan2_lookup(y, x);
            } else {
                ret = 0x8000 - atan2_lookup(x, y);
            }
        }
    } else {
        x = -x;
        if (y < 0) {
            y = -y;
            if (y >= x) {
                ret = 0x8000 + atan2_lookup(x, y);
            } else {
                ret = 0xC000 - atan2_lookup(y, x);
            }
        } else {
            if (y < x) {
                ret = 0xC000 + atan2_lookup(y, x);
            } else {
                ret = -atan2_lookup(x, y);
            }
        }
    }
    return ret;
}

/**
 * Compute the atan2 in radians by calling atan2s and converting the result.
 */
f32 atan2f(f32 y, f32 x) {
    return angle_to_radians(atan2s(y, x));
}

// Variables for a spline curve animation (used for the flight path in the grand star cutscene)
Vec4s *gSplineKeyframe;
f32 gSplineKeyframeFraction;
s32 gSplineState;

enum gSplineStates {
    CURVE_NONE,
    CURVE_BEGIN_1,
    CURVE_BEGIN_2,
    CURVE_MIDDLE,
    CURVE_END_1,
    CURVE_END_2
};

void spline_get_weights(Vec4f result, f32 t, UNUSED s32 c) {
    f32 tinv  = 1 - t;
    f32 tinv2 = tinv * tinv;
    f32 tinv3 = tinv2 * tinv;
    f32 t2 = t * t;
    f32 t3 = t2 * t;
    const f32 half    = (0.5f);
    const f32 quarter = (0.25f);
    const f32 sixth   = (1.0f / 6.0f);

    switch (gSplineState) {
        case CURVE_BEGIN_1:
            result[0] = tinv3;
            result[1] = ( t3 * (1.75f)) - (t2 * (4.5f)) + (t * (3.0f));
            result[2] = (-t3 * (11 / 12.0f)) + (t2 * (1.5f));
            result[3] = t3 * sixth;
            break;
        case CURVE_BEGIN_2:
            result[0] = tinv3 * quarter;
            result[1] = (t3 * (7 / 12.0f)) - (t2 * (1.25f)) + (t * quarter) + (7 / 12.0f);
            result[2] = (-t3 * half) + (t2 * half) + (t * half) + sixth;
            result[3] = t3 * sixth;
            break;
        case CURVE_MIDDLE:
            result[0] = tinv3 * sixth;
            result[1] = (t3 * half) - t2 + (4.0f / 6.0f);
            result[2] = (-t3 * half) + (t2 * half) + (t * half) + sixth;
            result[3] = t3 * sixth;
            break;
        case CURVE_END_1:
            result[0] = tinv3 * sixth;
            result[1] = (-tinv3 * half) + (tinv2 * half) + (tinv * half) + sixth;
            result[2] = (tinv3 * (7.0f / 12.0f)) - (tinv2 * (1.25f)) + (tinv * quarter) + (7.0f / 12.0f);
            result[3] = t3 * quarter;
            break;
        case CURVE_END_2:
            result[0] = tinv3 * sixth;
            result[1] = (-tinv3 * (11.0f / 12.0f)) + (tinv2 * (1.5f));
            result[2] = (tinv3 * (1.75f)) - (tinv2 * (4.5f)) + (tinv * (3.0f));
            result[3] = t3;
            break;
    }
}

void anim_spline_init(Vec4s *keyFrames) {
    gSplineKeyframe = keyFrames;
    gSplineKeyframeFraction = 0;
    gSplineState = CURVE_BEGIN_1;
}

s32 anim_spline_poll(Vec3f result) {
    Vec4f weights;
    s32 i, j;
    s32 hasEnded = FALSE;

    vec3_zero(result);
    spline_get_weights(weights, gSplineKeyframeFraction, gSplineState);
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 3; j++) {
            result[j] += weights[i] * gSplineKeyframe[i][j + 1];
        }
    }

    gSplineKeyframeFraction += (gSplineKeyframe[0][0] * (1.0f / 1000.0f));
    if (gSplineKeyframeFraction >= 1) {
        gSplineKeyframe++;
        gSplineKeyframeFraction--;
        switch (gSplineState) {
            case CURVE_END_2:
                hasEnded = TRUE;
                break;
            case CURVE_MIDDLE:
                if (gSplineKeyframe[2][0] == 0) {
                    gSplineState = CURVE_END_1;
                }
                break;
            default:
                gSplineState++;
                break;
        }
    }

    return hasEnded;
}
