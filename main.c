#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "kafka.c"
#include "process.c"

#include "io.h"

int num_kafkas = 0;
struct nosdk_kafka kafkas[64] = {0};

void handle_interrupt(int sig) {
    for (int i = 0; i < num_kafkas; i++) {
        nosdk_kafka_destroy(&kafkas[i]);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    struct nosdk_process_mgr mgr = {0};

    struct nosdk_io_mgr io_mgr = {0};
    if (nosdk_io_mgr_init(&io_mgr) != 0) {
        return 1;
    }

    signal(SIGINT, handle_interrupt);

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
        struct nosdk_kafka k = {0};

        switch (c) {
        case 's':
            k.type = CONSUMER;
            k.topic = owned_arg;
            kafkas[num_kafkas] = k;
            num_kafkas++;
            break;
        case 'p':
            k.type = PRODUCER;
            k.topic = owned_arg;
            kafkas[num_kafkas] = k;
            num_kafkas++;
            break;
        case 'c':
            nosdk_process_mgr_add(&mgr, optarg);
            break;
        }
    }

    for (int i = 0; i < num_kafkas; i++) {
        int ret = nosdk_kafka_init(&kafkas[i]);
        if (ret != 0) {
            return ret;
        }

        nosdk_kafka_start(&kafkas[i]);
    }

    nosdk_process_mgr_start(&mgr);

    for (int i = 0; i < num_kafkas; i++) {
        nosdk_kafka_wait(&kafkas[i]);
    }

    return 0;
}
