#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "process.h"

int main(int argc, char *argv[]) {
    struct nosdk_process_mgr mgr = {0};

    struct nosdk_io_mgr io_mgr = {0};
    if (nosdk_io_mgr_init(&io_mgr) != 0) {
        return 1;
    }

    int c;
    static struct option long_options[] = {
        {"subscribe", required_argument, NULL, 's'},
        {"publish", required_argument, NULL, 'p'},
        {"run", required_argument, NULL, 'c'},
        {0, 0, 0, 0},
    };

    while ((c = getopt_long(argc, argv, "s:p:c:", long_options, NULL)) != -1) {

        char *owned_arg = malloc(strlen(optarg));
        strcpy(owned_arg, optarg);

        switch (c) {
        case 's':
            if (nosdk_io_mgr_kafka_subscribe(&io_mgr, optarg) != 0) {
                return 1;
            }
            break;
        case 'p':
            if (nosdk_io_mgr_kafka_produce(&io_mgr, optarg) != 0) {
                return 1;
            }
            break;
        case 'c':
            nosdk_process_mgr_add(&mgr, optarg);
            break;
        }
    }

    nosdk_io_mgr_start(&io_mgr);

    nosdk_process_mgr_start(&mgr);

    return 0;
}
