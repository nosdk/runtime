#ifndef _NOSDK_UTIL_H
#define _NOSDK_UTIL_H

#include <stdarg.h>
#include <stdio.h>

extern int nosdk_debug_flag;

static inline int nosdk_debugf(const char *format, ...) {
    if (!nosdk_debug_flag) {
        return 0;
    }

    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}

#endif // _NOSDK_UTIL_H
