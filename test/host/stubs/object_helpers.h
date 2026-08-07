#ifndef HOST_STUB_OBJECT_HELPERS_H
#define HOST_STUB_OBJECT_HELPERS_H

#include "types.h"

extern struct Object *obj_nearest_object_with_behavior(const BehaviorScript *behavior);
extern void mark_object_for_deletion(struct Object *obj);

#endif
