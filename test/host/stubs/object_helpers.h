#ifndef HOST_STUB_OBJECT_HELPERS_H
#define HOST_STUB_OBJECT_HELPERS_H

#include "types.h"

extern struct Object *cur_obj_nearest_object_with_behavior(const BehaviorScript *behavior);
extern void obj_mark_for_deletion(struct Object *obj);

#endif
