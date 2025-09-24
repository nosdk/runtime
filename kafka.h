#ifndef _NOSDK_KAFKA_H
#define _NOSDK_KAFKA_H

#include <librdkafka/rdkafka.h>
#include <pthread.h>

#include "http.h"

#define MAX_KAFKA 16

enum nosdk_kafka_type {
    PRODUCER,
    CONSUMER,
};

struct nosdk_kafka {
    enum nosdk_kafka_type type;
    rd_kafka_t *rk;
    char *topic;
};

struct nosdk_kafka_thread_ctx {
    struct nosdk_kafka *k;
    char *root_dir;
    pthread_t thread;
};

struct nosdk_kafka_mgr {
    struct nosdk_kafka kafkas[MAX_KAFKA];
    int num_kafkas;
};

int nosdk_kafka_mgr_init();

int nosdk_kafka_init(struct nosdk_kafka *k);

// write the message headers to a regular file at path <filepath>.headers
int nosdk_kafka_write_headers(rd_kafka_message_t *msg, char *filepath);

void *nosdk_kafka_consumer_thread(void *arg);

void *nosdk_kafka_producer_thread(void *arg);

int nosdk_kafka_mgr_kafka_subscribe(char *topic);

struct nosdk_kafka *nosdk_kafka_mgr_get_consumer(char *topic);

int nosdk_kafka_mgr_kafka_produce(char *topic);

struct nosdk_kafka *nosdk_kafka_mgr_get_producer();

void nosdk_kafka_sub_handler(struct nosdk_http_request *req);

void nosdk_kafka_pub_handler(struct nosdk_http_request *req);

#endif // _NOSDK_KAFKA_H
