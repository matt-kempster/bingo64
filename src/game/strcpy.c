#include "strcpy.h"

#ifdef TARGET_N64
void strcpy(char *dst, const char *src) {
    // fuckin goddard
    while ((*dst++ = *src++)) {
        ;
    }
}
#endif
