#include "../util.h"
#include <stdlib.h>
#include <string.h>

int nosdk_debug_flag = 0;

void expect_equal(char *expected, char *s) {
    if (s == NULL) {
        printf("got NULL instead of '%s'\n", expected);
        exit(1);
    }
    if (strcmp(expected, s) != 0) {
        printf("expected '%s' == '%s'\n", s, expected);
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    expect_equal("a", json_extract_key("{\"id\": \"a\"}", "id"));
    expect_equal("123", json_extract_key("{\"id\": 123}", "id"));

    printf("all tests passed.\n");
    return 0;
}
