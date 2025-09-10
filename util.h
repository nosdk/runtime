#ifndef _NOSDK_UTIL_H
#define _NOSDK_UTIL_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

struct nosdk_string_buffer {
    char *data;
    int capacity;
    int size;
};

static inline struct nosdk_string_buffer *nosdk_string_buffer_new() {
    struct nosdk_string_buffer *sb = (struct nosdk_string_buffer *)malloc(
        sizeof(struct nosdk_string_buffer));
    sb->capacity = 1024;
    sb->size = 0;
    sb->data = (char *)malloc(1024);
    return sb;
}

static inline int nosdk_string_buffer_append(
    struct nosdk_string_buffer *sb, const char *format, ...) {
    va_list args;
    va_start(args, format);

    int needed = vsnprintf(NULL, 0, format, args);
    va_end(args);

    if (sb->size + needed + 1 >= sb->capacity) {
        sb->capacity = (sb->size + needed + 1) * 2;
        sb->data = (char *)realloc(sb->data, sb->capacity);
    }

    va_start(args, format);
    vsnprintf(sb->data + sb->size, sb->capacity - sb->size, format, args);
    va_end(args);

    sb->size += needed;

    sb->data[sb->size] = '\0';

    return 0;
}

static inline void nosdk_string_buffer_free(struct nosdk_string_buffer *sb) {
    free(sb->data);
    free(sb);
}

#endif // _NOSDK_UTIL_H
