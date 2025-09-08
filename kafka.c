#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
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

    if (rd_kafka_conf_set(
            conf, "auto.offset.reset", "latest", errstr, sizeof(errstr)) !=
        RD_KAFKA_CONF_OK) {

        fprintf(stderr, "config error: %s\n", errstr);
    }
    if (rd_kafka_conf_set(
            conf, "enable.auto.commit", "false", errstr, sizeof(errstr)) !=
        RD_KAFKA_CONF_OK) {

        fprintf(stderr, "config error: %s\n", errstr);
    }

    consumer->rk =
        rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    if (!consumer->rk) {
        fprintf(stderr, "failed to create consumer: %s\n", errstr);
        return 1;
    }

    // subscribe to topics
    subscription = rd_kafka_topic_partition_list_new(1);
    rd_kafka_topic_partition_t *partition = rd_kafka_topic_partition_list_add(
        subscription, consumer->topic, RD_KAFKA_PARTITION_UA);

    if (partition->err) {
        printf("subscription error: %s\n", rd_kafka_err2str(partition->err));
        return -1;
    }

    printf(
        "partition %d, offset is %" PRId64 "\n", partition->partition,
        partition->offset);

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

int nosdk_kafka_write_headers(rd_kafka_message_t *msg, char *filepath) {
    char headers_path[512];
    snprintf(headers_path, sizeof(headers_path), "%s.headers", filepath);

    int headers_fd = open(headers_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (headers_fd >= 0) {
        dprintf(headers_fd, "{");

        rd_kafka_headers_t *headers = rd_kafka_headers_new(10);
        rd_kafka_message_headers(msg, &headers);
        if (headers) {
            size_t header_count = rd_kafka_header_cnt(headers);
            for (size_t i = 0; i < header_count; i++) {
                const char *name;
                const void *value;
                size_t value_size;

                rd_kafka_header_get_all(headers, i, &name, &value, &value_size);
                dprintf(
                    headers_fd, "\"%s\":\"%.*s\",", name, (int)value_size,
                    (char *)value);
            }
        }

        // kafka metadata
        if (msg->key) {
            dprintf(
                headers_fd, "\"_key\":\"%.*s\",", (int)msg->key_len,
                (char *)msg->key);
        }

        dprintf(headers_fd, "\"_partition\":%d,\n", msg->partition);
        dprintf(headers_fd, "\"_offset\":%" PRId64 "}", msg->offset);
        close(headers_fd);
    }

    return 0;
}
