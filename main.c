#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "io.h"
#include "process.h"
#include "util.h"

int nosdk_debug_flag = 0;

int config_main(char *path) {
    struct nosdk_config *config = NULL;
    struct nosdk_process_mgr proc_mgr = {0};
    struct nosdk_io_mgr io_mgr = {0};

    if (nosdk_io_mgr_init(&io_mgr) != 0) {
        return 1;
    }

    proc_mgr.io_mgr = &io_mgr;

    if (nosdk_config_load(path, &config) != 0) {
        return 1;
    }

    for (int i = 0; i < config->processes_count; i++) {
        struct nosdk_process_config c = config->processes[i];
        struct nosdk_process p = {0};

        p.name = c.name;
        p.command = c.command;

        for (int j = 0; j < c.consume_count; j++) {
            struct nosdk_io_spec s = {
                .kind = KAFKA_CONSUME_TOPIC,
                .data = c.consume[j].topic,
            };
            nosdk_process_add_io(&p, s);
        }

        for (int j = 0; j < c.produce_count; j++) {
            struct nosdk_io_spec s = {
                .kind = KAFKA_PRODUCE_TOPIC,
                .data = c.produce[j].topic,
            };
            nosdk_process_add_io(&p, s);
        }

        struct nosdk_io_spec s = {
            .kind = POSTGRES,
            .data = NULL,
        };
        nosdk_process_add_io(&p, s);

        if (c.nproc == 0) {
            nosdk_process_mgr_add(&proc_mgr, p);
        } else {
            for (int n = 0; n < c.nproc; n++) {
                nosdk_process_mgr_add(&proc_mgr, p);
            }
        }
    }

    nosdk_process_mgr_start(&proc_mgr);

    nosdk_io_mgr_teardown(&io_mgr);

    return 0;
}

int main(int argc, char *argv[]) {
    int nproc = 1;
    char *config_path = NULL;

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
        {"produce", required_argument, NULL, 'p'},
        {"consume", required_argument, NULL, 'c'},
        {"run", required_argument, NULL, 'r'},
        {"nproc", required_argument, NULL, 'n'},
        {"debug", no_argument, NULL, 'd'},
        {"config", required_argument, NULL, 'f'},
        {0, 0, 0, 0},
    };

    while ((c = getopt_long(argc, argv, "p:c:r:n:f:d", long_options, NULL)) !=
           -1) {
        switch (c) {
        case 'c':
            s.kind = KAFKA_CONSUME_TOPIC;
            s.data = strdup(optarg);
            nosdk_process_add_io(&proc, s);
            break;
        case 'p':
            s.kind = KAFKA_PRODUCE_TOPIC;
            s.data = strdup(optarg);
            nosdk_process_add_io(&proc, s);
            break;
        case 'r':
            proc.command = strdup(optarg);
            break;
        case 'n':
            nproc = atoi(optarg);
            break;
        case 'd':
            nosdk_debug_flag = 1;
            break;
        case 'f':
            config_path = strdup(optarg);
            break;
        }
    }

    // load config from default path if it's there and we didn't
    // get a specific run command
    if (config_path == NULL && access("nosdk.yaml", F_OK) == 0 &&
        proc.command == NULL) {
        config_path = "nosdk.yaml";
    }

    if (config_path != NULL) {
        return config_main(config_path);
    }

    if (proc.command == NULL) {
        printf("no command specified\n");
        exit(1);
    }

    struct nosdk_io_spec http_spec = {
        .kind = POSTGRES,
        .data = NULL,
    };
    nosdk_process_add_io(&proc, http_spec);

    for (int i = 0; i < nproc; i++) {
        nosdk_process_mgr_add(&mgr, proc);
    }

    nosdk_process_mgr_start(&mgr);

    nosdk_io_mgr_teardown(&io_mgr);

    return 0;
}
