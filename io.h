#ifndef _NOSDK_IO_H
#define _NOSDK_IO_H

#define MAX_KAFKA 16

#include "kafka.h"

enum nosdk_io_kind {
    KAFKA_CONSUME_TOPIC,
    KAFKA_PRODUCE_TOPIC,
    POSTGRES,
};

// used to request initialization and setup of IO adapters
struct nosdk_io_spec {
    enum nosdk_io_kind kind;
    char *data;
};

struct nosdk_kafka_thread_ctx {
    struct nosdk_kafka *k;
    char *root_dir;
    pthread_t thread;
};

struct nosdk_io_process_ctx {
    int process_id;
    char *root_dir;
    int socket_fd;

    struct nosdk_kafka_thread_ctx kafka_contexts[MAX_KAFKA];
    int num_kafka_contexts;
};

struct nosdk_io_mgr {
    struct nosdk_kafka kafkas[MAX_KAFKA];
    int num_kafkas;
    struct nosdk_kafka_thread_ctx *kafka_contexts[128];
    int num_kafka_contexts;
};

typedef struct nosdk_io_mgr nosdk_io_mgr;

int nosdk_io_mgr_init(struct nosdk_io_mgr *mgr);

int nosdk_io_mgr_setup(
    struct nosdk_io_mgr *mgr, struct nosdk_io_spec spec, char *root_dir);

void nosdk_io_mgr_teardown(struct nosdk_io_mgr *mgr);

#endif // _NOSDK_IO_H
