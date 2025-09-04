#ifndef _NOSDK_KAFKA_H
#define _NOSDK_KAFKA_H

#include <librdkafka/rdkafka.h>
#include <pthread.h>

enum nosdk_kafka_type {
    PRODUCER,
    CONSUMER,
};

struct nosdk_kafka {
    enum nosdk_kafka_type type;
    rd_kafka_t *rk;
    pthread_t thread;
    char *topic;
};

int nosdk_kafka_init(struct nosdk_kafka *k);

void nosdk_kafka_start(struct nosdk_kafka *k);

void nosdk_kafka_wait(struct nosdk_kafka *k);

void nosdk_kafka_destroy(struct nosdk_kafka *k);

#endif // _NOSDK_KAFKA_H
