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

char *json_extract_key(char *buf, char *key) {
    char cur_str[64];
    int cur_str_pos = 0;

    int brace_depth = 0;
    int bracket_depth = 0;
    bool found_key = false;

    for (int i = 0; i < strlen(buf); i++) {
        char this_char = buf[i];

        if (this_char == '{')
            brace_depth++;
        if (this_char == '}')
            brace_depth--;
        if (this_char == '[')
            bracket_depth++;
        if (this_char == ']')
            bracket_depth--;

        if (brace_depth < 2 && bracket_depth == 0) {
            // top level

            if (this_char == ':' || this_char == ',' || this_char == '}') {
                cur_str[cur_str_pos] = '\0';

                if (found_key) {
                    return strdup(cur_str);
                }

                if (strcmp(cur_str, key) == 0) {
                    found_key = true;
                }

                cur_str_pos = 0;
            } else if (
                this_char != '"' && this_char != '{' && this_char != '}' &&
                this_char != ',' && this_char != ' ') {
                if (cur_str_pos < 63) {
                    cur_str[cur_str_pos] = this_char;
                }
                cur_str_pos++;
            }
        }
    }

    return NULL;
}
