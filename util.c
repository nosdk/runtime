#include "util.h"

int json_array_next_item(
    struct json_array_iter *iter, int *start_pos, int *len) {
    int start = 0;
    int end = 0;
    int depth = 0;
    int in_str = 0;
    char last_char = '\0';

    while (end == 0 && iter->pos < iter->data_len) {
        char this_char = iter->data[iter->pos];

        if (this_char == '{' && !in_str) {
            start = iter->pos;
            depth++;
        } else if (this_char == '}' && !in_str) {
            depth--;
        } else if (this_char == '"' && !in_str) {
            in_str = 1;
        } else if (this_char == '"' && in_str && last_char != '\\') {
            in_str = 0;
        }

        iter->pos++;

        if (start != 0 && depth == 0) {
            end = iter->pos;
            break;
        }

        last_char = this_char;
    }

    *start_pos = start;
    *len = (end - start);

    return start;
}
