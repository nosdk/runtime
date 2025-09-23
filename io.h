#ifndef _NOSDK_IO_H
#define _NOSDK_IO_H

#define MAX_PROCS 100

#include "http.h"
#include "kafka.h"

enum nosdk_io_kind {
    KAFKA_CONSUME_TOPIC,
    KAFKA_PRODUCE_TOPIC,
    POSTGRES,
    S3,
};

// used to request initialization and setup of IO adapters
struct nosdk_io_spec {
    enum nosdk_io_kind kind;
    char *data;
};

struct nosdk_io_process_ctx {
    int process_id;
    char *root_dir;

    struct nosdk_http_server *server;
    pthread_t http_thread;

    struct nosdk_kafka_thread_ctx *kafka_contexts[MAX_KAFKA];
    int num_kafka_contexts;
};

struct nosdk_io_mgr {
    struct nosdk_io_process_ctx contexts[MAX_PROCS];
    int num_contexts;
};

typedef struct nosdk_io_mgr nosdk_io_mgr;

int nosdk_io_mgr_init(struct nosdk_io_mgr *mgr);

struct nosdk_io_process_ctx *nosdk_io_process_ctx_new(struct nosdk_io_mgr *mgr);

int nosdk_io_mgr_setup(
    struct nosdk_io_mgr *mgr,
    struct nosdk_io_process_ctx *ctx,
    struct nosdk_io_spec spec);

void nosdk_io_mgr_start(struct nosdk_io_mgr *mgr);

void nosdk_io_mgr_teardown(struct nosdk_io_mgr *mgr);

#endif // _NOSDK_IO_H
