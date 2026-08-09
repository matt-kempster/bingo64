#ifndef _STRCPY_H
#define _STRCPY_H

#ifdef TARGET_N64
void strcpy(char *dst, const char *src);
#else
// The PC port links against the host libc.
#include <string.h>
#endif

#endif /* _STRCPY_H */
