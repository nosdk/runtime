#include <errno.h>
#include <fcntl.h>
#include <librdkafka/rdkafka.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kafka.h"

void kafka_conf_must_set(
    rd_kafka_conf_t *conf, const char *property, const char *envvar) {
    char err[512];
    char *value = getenv(envvar);
    if (value == NULL || strlen(value) == 0) {
        fprintf(stderr, "missing environment variable: %s\n", envvar);
        exit(1);
    }
    int ret = rd_kafka_conf_set(conf, property, value, err, sizeof(err));
    if (ret != RD_KAFKA_CONF_OK) {
        fprintf(stderr, "configuration failure: %s\n", err);
        exit(1);
    }
}

int nosdk_kafka_consumer_init(struct nosdk_kafka *consumer) {
    rd_kafka_conf_t *conf;
    rd_kafka_topic_partition_list_t *subscription;
    char errstr[512];

    conf = rd_kafka_conf_new();

    kafka_conf_must_set(
        conf, "bootstrap.servers", "NOSDK_KAFKA_BOOTSTRAP_SERVERS");
    kafka_conf_must_set(conf, "group.id", "NOSDK_KAFKA_GROUP_ID");

    rd_kafka_conf_set(
        conf, "auto.offset.reset", "earliest", errstr, sizeof(errstr));
    rd_kafka_conf_set(
        conf, "enable.auto.commit", "false", errstr, sizeof(errstr));

    consumer->rk =
        rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    if (!consumer->rk) {
        fprintf(stderr, "failed to create consumer: %s\n", errstr);
        return 1;
    }

    // subscribe to topics
    subscription = rd_kafka_topic_partition_list_new(1);
    rd_kafka_topic_partition_list_add(
        subscription, consumer->topic, RD_KAFKA_PARTITION_UA);

    rd_kafka_resp_err_t err = rd_kafka_subscribe(consumer->rk, subscription);
    if (err) {
        fprintf(
            stderr, "failed to subscribe to %s: %s\n", consumer->topic,
            rd_kafka_err2str(err));
        return 1;
    }

    return 0;
}

int nosdk_kafka_producer_init(struct nosdk_kafka *producer) {
    rd_kafka_conf_t *conf;
    char errstr[512];

    conf = rd_kafka_conf_new();

    kafka_conf_must_set(
        conf, "bootstrap.servers", "NOSDK_KAFKA_BOOTSTRAP_SERVERS");

    producer->rk =
        rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (!producer->rk) {
        fprintf(stderr, "failed to create producer: %s\n", errstr);
        return 1;
    }

    return 0;
}

int nosdk_kafka_init(struct nosdk_kafka *k) {
    if (k->type == CONSUMER) {
        return nosdk_kafka_consumer_init(k);
    } else if (k->type == PRODUCER) {
        return nosdk_kafka_producer_init(k);
    }
    return -1;
}
