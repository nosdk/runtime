#include <fcntl.h>
#include <librdkafka/rdkafka.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "http.h"
#include "io.h"
#include "postgres.h"
#include "s3.h"
#include "util.h"

int nosdk_io_mgr_init(struct nosdk_io_mgr *mgr) { return 0; }

struct nosdk_io_process_ctx *
nosdk_io_process_ctx_new(struct nosdk_io_mgr *mgr) {
    mgr->contexts[mgr->num_contexts].process_id = mgr->num_contexts;
    mgr->num_contexts++;
    return &mgr->contexts[mgr->num_contexts - 1];
}

int nosdk_io_mgr_add_kafka(struct nosdk_io_mgr *mgr, struct nosdk_kafka k) {
    if (mgr->num_kafkas >= MAX_KAFKA) {
        printf("kafka process limit has been reached\n");
        return 1;
    }

    int ret = nosdk_kafka_init(&k);
    if (ret != 0) {
        return ret;
    }
    mgr->kafkas[mgr->num_kafkas] = k;
    mgr->num_kafkas++;
    return 0;
}

struct nosdk_kafka *
nosdk_io_mgr_get_consumer(struct nosdk_io_mgr *mgr, char *topic) {

    for (int i = 0; i < mgr->num_kafkas; i++) {
        if (strcmp(mgr->kafkas[i].topic, topic) == 0) {
            return &mgr->kafkas[i];
        }
    }

    return NULL;
}

int nosdk_io_mgr_kafka_subscribe(struct nosdk_io_mgr *mgr, char *topic) {
    if (nosdk_io_mgr_get_consumer(mgr, topic) != NULL) {
        return 0;
    }

    struct nosdk_kafka k = {
        .type = CONSUMER,
        .topic = strdup(topic),
    };

    return nosdk_io_mgr_add_kafka(mgr, k);
}

struct nosdk_kafka *nosdk_io_mgr_get_producer(struct nosdk_io_mgr *mgr) {
    for (int i = 0; i < mgr->num_kafkas; i++) {
        if (mgr->kafkas[i].type == PRODUCER) {
            return &mgr->kafkas[i];
        }
    }
    return NULL;
}

int nosdk_io_mgr_kafka_produce(struct nosdk_io_mgr *mgr, char *topic) {
    if (nosdk_io_mgr_get_producer(mgr) != NULL) {
        return 0;
    }
    struct nosdk_kafka k = {
        .type = PRODUCER,
        .topic = strdup(topic),
    };

    return nosdk_io_mgr_add_kafka(mgr, k);
}

int nosdk_io_mgr_setup(
    struct nosdk_io_mgr *mgr,
    struct nosdk_io_process_ctx *ctx,
    struct nosdk_io_spec spec) {

    int ret;

    struct nosdk_kafka_thread_ctx *kthread =
        malloc(sizeof(struct nosdk_kafka_thread_ctx));
    kthread->root_dir = ctx->root_dir;
    kthread->thread = 0;
    kthread->k = NULL;

    if (spec.kind == KAFKA_CONSUME_TOPIC) {
        ret = nosdk_io_mgr_kafka_subscribe(mgr, spec.data);
        if (ret != 0) {
            return ret;
        }

        kthread->k = nosdk_io_mgr_get_consumer(mgr, spec.data);
        pthread_create(
            &kthread->thread, NULL, nosdk_kafka_consumer_thread, kthread);

        return 0;
    } else if (spec.kind == KAFKA_PRODUCE_TOPIC) {
        ret = nosdk_io_mgr_kafka_produce(mgr, spec.data);
        if (ret != 0) {
            return ret;
        }

        kthread->k = nosdk_io_mgr_get_producer(mgr);
        pthread_create(
            &kthread->thread, NULL, nosdk_kafka_producer_thread, kthread);

        return 0;
    } else if (spec.kind == POSTGRES) {
        if (ctx->server == NULL) {
            ctx->server = nosdk_http_server_new();
        }

        struct nosdk_http_handler handler = {
            .prefix = "/db",
            .handler = nosdk_pg_handler,
        };

        if (nosdk_http_server_handle(ctx->server, handler) != 0) {
            return -1;
        }
    } else if (spec.kind == S3) {
        if (ctx->server == NULL) {
            ctx->server = nosdk_http_server_new();
        }

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
    for (int i = 0; i < mgr->num_kafkas; i++) {
        nosdk_debugf("destroying kafka client %d\n", i);
        if (mgr->kafkas[i].type == PRODUCER) {
            rd_kafka_flush(mgr->kafkas[i].rk, 500);
        }
        rd_kafka_destroy(mgr->kafkas[i].rk);
    }

    for (int i = 0; i < mgr->num_contexts; i++) {
        if (mgr->contexts[i].server->socket_fd != 0) {
            close(mgr->contexts[i].server->socket_fd);
        }
    }
}
