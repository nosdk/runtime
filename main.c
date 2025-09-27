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

struct nosdk_config *load_config(char *path) {
    struct nosdk_config *config = NULL;

    if (nosdk_config_load(path, &config) != 0) {
        return NULL;
    }

    return config;
}

int config_main(struct nosdk_config *config, bool is_yaml) {
    struct nosdk_process_mgr proc_mgr = {0};
    struct nosdk_io_mgr io_mgr = {0};

    if (nosdk_io_mgr_init(&io_mgr) != 0) {
        return 1;
    }

    proc_mgr.io_mgr = &io_mgr;

    for (int i = 0; i < config->processes_count; i++) {
        struct nosdk_process_config c = config->processes[i];
        struct nosdk_process p = {0};

        p.name = c.name;
        p.command = c.command;

        for (int j = 0; j < c.consume_count; j++) {
            struct nosdk_io_spec s = {
                .kind = KAFKA_CONSUME_TOPIC,
                .interface = c.consume[j].interface,
                .data = c.consume[j].topic,
            };
            nosdk_process_add_io(&p, s);
        }

        for (int j = 0; j < c.produce_count; j++) {
            struct nosdk_io_spec s = {
                .kind = KAFKA_PRODUCE_TOPIC,
                .interface = c.produce[j].interface,
                .data = c.produce[j].topic,
            };
            nosdk_process_add_io(&p, s);
        }

        struct nosdk_io_spec s = {
            .kind = POSTGRES,
            .data = NULL,
        };
        nosdk_process_add_io(&p, s);

        struct nosdk_io_spec s3 = {
            .kind = S3,
            .data = NULL,
        };
        nosdk_process_add_io(&p, s3);

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

    if (is_yaml)
        nosdk_config_destroy(config);

    return 0;
}

int main(int argc, char *argv[]) {
    char *config_path = NULL;

    struct nosdk_process_config p_config = {0};
    p_config.name = "cmdline";
    p_config.consume = malloc(sizeof(struct nosdk_messaging_config) * 16);
    p_config.produce = malloc(sizeof(struct nosdk_messaging_config) * 16);

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
            p_config.consume[p_config.consume_count].topic = strdup(optarg);
            p_config.consume[p_config.consume_count].interface = HTTP;
            p_config.consume_count++;
            break;
        case 'p':
            p_config.produce[p_config.produce_count].topic = strdup(optarg);
            p_config.produce[p_config.produce_count].interface = HTTP;
            p_config.produce_count++;
            break;
        case 'r':
            p_config.command = strdup(optarg);
            break;
        case 'n':
            p_config.nproc = atoi(optarg);
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
        p_config.command == NULL) {
        config_path = "nosdk.yaml";
    }

    if (config_path != NULL) {
        free(p_config.consume);
        free(p_config.produce);
        struct nosdk_config *config = load_config(config_path);
        if (config == NULL) {
            printf("failed to parsed config file: %s\n", config_path);
            exit(1);
        }
        return config_main(config, true);
    } else if (p_config.command != NULL) {
        struct nosdk_config config = {0};
        config.processes = &p_config;
        config.processes_count = 1;
        config_main(&config, false);
        for (int i = 0; i < 16; i++) {
            if (i < p_config.consume_count) {
                free(p_config.consume[i].topic);
            }
            if (i < p_config.produce_count) {
                free(p_config.produce[i].topic);
            }
        }
        free(p_config.consume);
        free(p_config.produce);
        free(p_config.command);
        return 0;
    }

    free(p_config.consume);
    free(p_config.produce);
    printf("no config file and no process specified.\n");

    return 1;
}
