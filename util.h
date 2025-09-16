#ifndef _NOSDK_UTIL_H
#define _NOSDK_UTIL_H

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// https://stackoverflow.com/a/14530993
static inline void urldecode2(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a')
                a -= 'a' - 'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a' - 'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

// an iterator that can be used to step through JSON array bytes
// and retrieve the start index and length of each object within
// the array
struct json_array_iter {
    char *data;
    int data_len;
    int pos;
};

int json_array_next_item(
    struct json_array_iter *iter, int *start_pos, int *len);

// extract a string value for the given top-level string key
// from a JSON object
// null if the key is not present in the buffer
char *json_extract_key(char *buf, char *key);

#endif // _NOSDK_UTIL_H
