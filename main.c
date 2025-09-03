#include <librdkafka/rdkafka.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void kafka_conf_must_set(
    rd_kafka_conf_t *conf, const char *property, const char *envvar) {
    char err[512];
    char *value = getenv(envvar);
    if (value == NULL || strlen(value) == 0) {
        fprintf(stderr, "missing environment variable: %s\n", envvar);
    }
    int ret = rd_kafka_conf_set(conf, property, value, err, sizeof(err));
    if (ret != RD_KAFKA_CONF_OK) {
        fprintf(stderr, "configuration failure: %s\n", err);
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    rd_kafka_t *rk;
    rd_kafka_conf_t *conf;
    rd_kafka_topic_partition_list_t *subscription;
    char errstr[512];
    char *topic_name = "records";

    conf = rd_kafka_conf_new();

    kafka_conf_must_set(
        conf, "bootstrap.servers", "NOSDK_KAFKA_BOOTSTRAP_SERVERS");
    kafka_conf_must_set(conf, "group.id", "NOSDK_KAFKA_GROUP_ID");

    rd_kafka_conf_set(
        conf, "auto.offset.reset", "earliest", errstr, sizeof(errstr));
    rd_kafka_conf_set(
        conf, "enable.auto.commit", "false", errstr, sizeof(errstr));

    rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    if (!rk) {
        fprintf(stderr, "failed to create consumer: %s\n", errstr);
        return 1;
    }

    // subscribe to topics
    subscription = rd_kafka_topic_partition_list_new(1);
    rd_kafka_topic_partition_list_add(
        subscription, topic_name, RD_KAFKA_PARTITION_UA);

    rd_kafka_resp_err_t err = rd_kafka_subscribe(rk, subscription);
    if (err) {
        fprintf(
            stderr, "failed to subscribe to %s: %s\n", topic_name,
            rd_kafka_err2str(err));
        return 1;
    }

    while (1) {
        rd_kafka_message_t *msg = rd_kafka_consumer_poll(rk, 1000);
        if (!msg) {
            fprintf(stderr, "no messages...\n");
            continue;
        }

        if (msg->err) {
            fprintf(
                stderr, "consumer error: %s\n", rd_kafka_message_errstr(msg));
        }

        printf("got a message? %.*s\n", (int)msg->len, (char *)msg->payload);
        err = rd_kafka_commit_message(rk, msg, 0);
        if (err) {
            fprintf(
                stderr, "failed to commit offset: %s\n", rd_kafka_err2str(err));
        }

        rd_kafka_message_destroy(msg);
    }

    return 0;
}
