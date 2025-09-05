#include <_string.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "process.h"

int main(int argc, char *argv[]) {
    int nproc = 1;

    struct nosdk_process_mgr mgr = {0};

    struct nosdk_io_mgr io_mgr = {0};
    if (nosdk_io_mgr_init(&io_mgr) != 0) {
        return 1;
    }

    mgr.io_mgr = &io_mgr;

    struct nosdk_process proc = {0};

    struct nosdk_io_spec s = {0};

    int c;
    static struct option long_options[] = {
        {"subscribe", required_argument, NULL, 's'},
        {"publish", required_argument, NULL, 'p'},
        {"run", required_argument, NULL, 'c'},
        {"nproc", required_argument, NULL, 'n'},
        {0, 0, 0, 0},
    };

    while ((c = getopt_long(argc, argv, "s:p:c:n:", long_options, NULL)) !=
           -1) {
        switch (c) {
        case 's':
            s.kind = KAFKA_CONSUME_TOPIC;
            s.data = strdup(optarg);
            nosdk_process_add_io(&proc, s);
            break;
        case 'p':
            s.kind = KAFKA_PRODUCE_TOPIC;
            s.data = strdup(optarg);
            nosdk_process_add_io(&proc, s);
            break;
        case 'c':
            proc.command = strdup(optarg);
            break;
        case 'n':
            nproc = atoi(optarg);
        }
    }

    if (proc.command == NULL) {
        printf("no command specified\n");
        exit(1);
    }

    for (int i = 0; i < nproc; i++) {
        nosdk_process_mgr_add(&mgr, proc);
    }

    nosdk_process_mgr_start(&mgr);

    return 0;
}
