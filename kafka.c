#include <errno.h>
#include <fcntl.h>
#include <librdkafka/rdkafka.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct nosdk_kafka_consumer {
    rd_kafka_t *rk;
    pthread_t thread;
    char *topic;
};

void *nosdk_kafka_consumer_thread(void *arg) {
    struct nosdk_kafka_consumer *consumer = (struct nosdk_kafka_consumer *)arg;
    printf("creating consumer fifo: %s\n", consumer->topic);

    // ignore SIGPIPE so we can handle it
    signal(SIGPIPE, SIG_IGN);

    struct stat st;
    if (stat(consumer->topic, &st) == -1 && errno == ENOENT) {
        if (mkfifo(consumer->topic, S_IRWXU) < 0) {
            perror("fifo creation");
            return NULL;
        }
    }

    while (1) {
        int write_fd = open(consumer->topic, O_WRONLY);
        if (write_fd < 0) {
            perror("opening fifo");
            break;
        }

        while (1) {
            rd_kafka_message_t *msg = rd_kafka_consumer_poll(consumer->rk, 500);

            if (!msg) {
                continue;
            }

            ssize_t result = write(write_fd, msg->payload, msg->len);
            if (result > 0) {
                rd_kafka_commit_message(consumer->rk, msg, 0);
            }
            close(write_fd);
            rd_kafka_message_destroy(msg);
            break;
        }
    }

    return NULL;
}

void nosdk_kafka_consumer_start(struct nosdk_kafka_consumer *consumer) {
    pthread_create(
        &consumer->thread, NULL, nosdk_kafka_consumer_thread, consumer);
}

void nosdk_kafka_consumer_wait(struct nosdk_kafka_consumer *consumer) {
    pthread_join(consumer->thread, NULL);
}

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

int nosdk_kafka_consumer_init(struct nosdk_kafka_consumer *consumer) {
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
