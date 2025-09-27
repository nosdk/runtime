#include <fcntl.h>
#include <librdkafka/rdkafka.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "http.h"
#include "io.h"
#include "kafka.h"
#include "postgres.h"
#include "s3.h"

int nosdk_io_mgr_init(struct nosdk_io_mgr *mgr) {
    if (nosdk_kafka_mgr_init() != 0) {
        return -1;
    }

    return 0;
}

struct nosdk_io_process_ctx *
nosdk_io_process_ctx_new(struct nosdk_io_mgr *mgr) {
    mgr->contexts[mgr->num_contexts].process_id = mgr->num_contexts;
    mgr->num_contexts++;
    return &mgr->contexts[mgr->num_contexts - 1];
}

int nosdk_io_mgr_setup(
    struct nosdk_io_mgr *mgr,
    struct nosdk_io_process_ctx *ctx,
    struct nosdk_io_spec spec) {

    int ret;

    if (ctx->server == NULL) {
        ctx->server = nosdk_http_server_new();
    }

    if (spec.kind == KAFKA_CONSUME_TOPIC) {
        ret = nosdk_kafka_mgr_kafka_subscribe(spec.data);
        if (ret != 0) {
            return ret;
        }

        if (spec.interface == FS) {
            struct nosdk_kafka_thread_ctx *kthread =
                nosdk_kafka_mgr_make_thread(ctx->root_dir);
            kthread->k = nosdk_kafka_mgr_get_consumer(spec.data);
            pthread_create(
                &kthread->thread, NULL, nosdk_kafka_consumer_thread, kthread);
        } else {
            struct nosdk_http_handler handler = {
                .prefix = "/msg",
                .handler = nosdk_kafka_handler,
            };

            if (nosdk_http_server_handle(ctx->server, handler) != 0) {
                return -1;
            }
        }

        return 0;
    } else if (spec.kind == KAFKA_PRODUCE_TOPIC) {

        ret = nosdk_kafka_mgr_kafka_produce(spec.data);
        if (ret != 0) {
            return ret;
        }

        if (spec.interface == FS) {
            struct nosdk_kafka_thread_ctx *kthread =
                nosdk_kafka_mgr_make_thread(ctx->root_dir);
            kthread->k = nosdk_kafka_mgr_get_producer();
            pthread_create(
                &kthread->thread, NULL, nosdk_kafka_producer_thread, kthread);
        } else {
            struct nosdk_http_handler handler = {
                .prefix = "/msg",
                .handler = nosdk_kafka_handler,
            };

            if (nosdk_http_server_handle(ctx->server, handler) != 0) {
                return -1;
            }
        }

        return 0;
    } else if (spec.kind == POSTGRES) {
        struct nosdk_http_handler handler = {
            .prefix = "/db",
            .handler = nosdk_pg_handler,
        };

        if (nosdk_http_server_handle(ctx->server, handler) != 0) {
            return -1;
        }
    } else if (spec.kind == S3) {

        struct nosdk_http_handler handler = {
            .prefix = "/blob",
            .handler = nosdk_s3_handler,
        };

        if (nosdk_http_server_handle(ctx->server, handler) != 0) {
            return -1;
        }
    }

    return 0;
}

void *nosdk_io_mgr_http_thread(void *arg) {
    struct nosdk_io_process_ctx *ctx = (struct nosdk_io_process_ctx *)arg;

    nosdk_http_server_start(ctx->server);

    return NULL;
}

void nosdk_io_mgr_start(struct nosdk_io_mgr *mgr) {
    for (int i = 0; i < mgr->num_contexts; i++) {
        pthread_create(
            &mgr->contexts[i].http_thread, NULL, nosdk_io_mgr_http_thread,
            (void *)&mgr->contexts[i]);
    }
}

void nosdk_io_mgr_teardown(struct nosdk_io_mgr *mgr) {

    for (int i = 0; i < mgr->num_contexts; i++) {
        nosdk_http_server_destroy(mgr->contexts[i].server);
    }

    nosdk_kafka_mgr_teardown();
}
