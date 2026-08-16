
/**
 * This is the behavior file for the tilting inverted pyramids in BitFS/LLL.
 * The object essentially just tilts and moves Mario with it.
 */

#if defined(ALO) || PLATFORM_DISPLACEMENT_2

/**
 * Initialize the object's transform matrix with Y being up.
 */
void bhv_platform_normals_init(void) {
    vec3f_set(&o->oTiltingPyramidNormalX, 0.0f, 1.0f, 0.0f);
    mtxf_align_terrain_normal(o->transform, &o->oTiltingPyramidNormalX, &o->oPosX, 0);
}

/**
 * Main behavior for the tilting pyramids in LLL/BitFS. These platforms calculate rough normals from Mario's position,
 * then gradually tilt back moving Mario with them.
 */
void bhv_tilting_inverted_pyramid_loop(void) {
#if !PLATFORM_DISPLACEMENT_2
    Vec3f posBeforeRotation, posAfterRotation;
    Vec3f marioPos, dist;
#endif
    Vec3f targetNormal;
    Mat4 *transform = &o->transform;
    s32 marioOnPlatform = (gMarioObject->platform == o);

    if (marioOnPlatform) {
#if !PLATFORM_DISPLACEMENT_2
        // Target the normal in Mario's direction
        vec3_diff(dist, gMarioStates[0].pos, &o->oPosX);

        // Get Mario's position before the rotation
        vec3f_copy(marioPos, gMarioStates[0].pos);

        linear_mtxf_mul_vec3f(*transform, posBeforeRotation, dist);
        targetNormal[0] = dist[0];
        targetNormal[2] = dist[2];
#else // PLATFORM_DISPLACEMENT_2
        targetNormal[0] = gMarioStates[0].pos[0] - o->oPosX;
        targetNormal[2] = gMarioStates[0].pos[2] - o->oPosZ;
#endif
        targetNormal[1] = 500.0f;
        vec3f_normalize(targetNormal);
    } else {
        // Target normal is directly upwards when Mario is not on the platform.
        vec3f_set(targetNormal, 0.0f, 1.0f, 0.0f);
    }

    // Approach the normals by 0.01f towards the new goal, then create a transform matrix and orient the object.
    // Outside of the other conditionals since it needs to tilt regardless of whether Mario is on.
    approach_f32_symmetric_bool(&o->oTiltingPyramidNormalX, targetNormal[0], 0.01f);
    approach_f32_symmetric_bool(&o->oTiltingPyramidNormalY, targetNormal[1], 0.01f);
    approach_f32_symmetric_bool(&o->oTiltingPyramidNormalZ, targetNormal[2], 0.01f);
    vec3f_normalize(&o->oTiltingPyramidNormalX);
    mtxf_align_terrain_normal(*transform, &o->oTiltingPyramidNormalX, &o->oPosX, 0x0);

#if !PLATFORM_DISPLACEMENT_2
    // If Mario is on the platform, adjust his position for the platform tilt.
    if (marioOnPlatform) {
        linear_mtxf_mul_vec3f(*transform, posAfterRotation, dist);
        marioPos[0] += posAfterRotation[0] - posBeforeRotation[0];
        marioPos[1] += posAfterRotation[1] - posBeforeRotation[1];
        marioPos[2] += posAfterRotation[2] - posBeforeRotation[2];
        vec3f_copy(gMarioStates[0].pos, marioPos);
    }
#endif

    o->header.gfx.throwMatrix = transform;
}

#else // vanilla

/**
 * Creates a transform matrix on a variable passed in from given normals
 * and the object's position.
 */
static void tilting_pyramid_create_transform_from_normals(Mat4 transform, f32 xNorm, f32 yNorm, f32 zNorm) {
    Vec3f normal;
    Vec3f pos;

    pos[0] = o->oPosX;
    pos[1] = o->oPosY;
    pos[2] = o->oPosZ;

    normal[0] = xNorm;
    normal[1] = yNorm;
    normal[2] = zNorm;

    mtxf_align_terrain_normal(transform, normal, pos, 0);
}

/**
 * Initialize the object's transform matrix with Y being up.
 */
void bhv_platform_normals_init(void) {
    Mat4 *transform = &o->transform;

    o->oTiltingPyramidNormalX = 0.0f;
    o->oTiltingPyramidNormalY = 1.0f;
    o->oTiltingPyramidNormalZ = 0.0f;

    tilting_pyramid_create_transform_from_normals(*transform, 0.0f, 1.0f, 0.0f);
}

/**
 * Returns a value that is src incremented/decremented by inc towards goal
 * until goal is reached. Does not overshoot.
 */
static f32 tilting_pyramid_approach_by_increment(f32 goal, f32 src, f32 inc) {
    f32 newVal;

    if (src <= goal) {
        if (goal - src < inc) {
            newVal = goal;
        } else {
            newVal = src + inc;
        }
    } else if (goal - src > -inc) {
        newVal = goal;
    } else {
        newVal = src - inc;
    }

    return newVal;
}

/**
 * Main behavior for the tilting pyramids in LLL/BitFS. These platforms calculate
 * rough normals from Mario's position, then gradually tilt back moving Mario with them.
 */
void bhv_tilting_inverted_pyramid_loop(void) {
    f32 dx;
    f32 dy;
    f32 dz;
    f32 d;

    Vec3f dist;                // Mario's position relative to the platform
    Vec3f posBeforeRotation;
    Vec3f posAfterRotation;

    // Mario's position
    f32 mx;
    f32 my;
    f32 mz;

    s32 marioOnPlatform = FALSE;
    Mat4 *transform = &o->transform;

    if (gMarioObject->platform == o) {
        get_mario_pos(&mx, &my, &mz);

        dist[0] = gMarioObject->oPosX - o->oPosX;
        dist[1] = gMarioObject->oPosY - o->oPosY;
        dist[2] = gMarioObject->oPosZ - o->oPosZ;
        linear_mtxf_mul_vec3f(*transform, posBeforeRotation, dist);

        dx = gMarioObject->oPosX - o->oPosX;
        dy = 500.0f;
        dz = gMarioObject->oPosZ - o->oPosZ;
        d = sqrtf(dx * dx + dy * dy + dz * dz);

        //! Always true since dy = 500, making d >= 500.
        if (d != 0.0f) {
            // Normalizing
            d = 1.0 / d;
            dx *= d;
            dy *= d;
            dz *= d;
        } else {
            dx = 0.0f;
            dy = 1.0f;
            dz = 0.0f;
        }

        if (o->oTiltingPyramidMarioOnPlatform == TRUE) {
            marioOnPlatform++;
        }

        o->oTiltingPyramidMarioOnPlatform = TRUE;
    } else {
        dx = 0.0f;
        dy = 1.0f;
        dz = 0.0f;
        o->oTiltingPyramidMarioOnPlatform = FALSE;
    }

    // Approach the normals by 0.01f towards the new goal, then create a transform matrix and orient the object.
    // Outside of the other conditionals since it needs to tilt regardless of whether Mario is on.
    o->oTiltingPyramidNormalX = tilting_pyramid_approach_by_increment(dx, o->oTiltingPyramidNormalX, 0.01f);
    o->oTiltingPyramidNormalY = tilting_pyramid_approach_by_increment(dy, o->oTiltingPyramidNormalY, 0.01f);
    o->oTiltingPyramidNormalZ = tilting_pyramid_approach_by_increment(dz, o->oTiltingPyramidNormalZ, 0.01f);
    tilting_pyramid_create_transform_from_normals(*transform, o->oTiltingPyramidNormalX,
                                                  o->oTiltingPyramidNormalY, o->oTiltingPyramidNormalZ);

    // If Mario is on the platform, adjust his position for the platform tilt.
    // (Only from his second frame on the platform — vanilla quirk.)
    if (marioOnPlatform != FALSE) {
        linear_mtxf_mul_vec3f(*transform, posAfterRotation, dist);
        mx += posAfterRotation[0] - posBeforeRotation[0];
        my += posAfterRotation[1] - posBeforeRotation[1];
        mz += posAfterRotation[2] - posBeforeRotation[2];
        set_mario_pos(mx, my, mz);
    }

    o->header.gfx.throwMatrix = transform;
}

#endif
