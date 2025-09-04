#include <errno.h>
#include <fcntl.h>
#include <librdkafka/rdkafka.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

int nosdk_ensure_fifo(char *path) {
    struct stat st;
    if (stat(path, &st) == -1 && errno == ENOENT) {
        if (mkfifo(path, S_IRWXU) < 0) {
            perror("fifo creation");
            return -1;
        }
    }
    return 0;
}

void *nosdk_kafka_consumer_thread(void *arg) {
    struct nosdk_kafka *consumer = (struct nosdk_kafka *)arg;
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

void *nosdk_kafka_producer_thread(void *arg) {
    struct nosdk_kafka *producer = (struct nosdk_kafka *)arg;
    printf("creating producer fifo: %s\n", producer->topic);

    if (nosdk_ensure_fifo(producer->topic) < 0) {
        return NULL;
    }

    // kafka max message size is 1mb!
    char *msg_buf = malloc(1000 * 1000);

    while (1) {
        int read_fd = open(producer->topic, O_RDONLY);
        if (read_fd < 0) {
            perror("opening fifo");
            break;
        }

        ssize_t result = read(read_fd, msg_buf, 1000 * 1000);

        rd_kafka_producev(
            producer->rk, RD_KAFKA_V_TOPIC(producer->topic),
            RD_KAFKA_V_VALUE(msg_buf, result), RD_KAFKA_V_END);

        close(read_fd);
    }

    rd_kafka_flush(producer->rk, 5000);
    free(msg_buf);
    return NULL;
}

void nosdk_kafka_start(struct nosdk_kafka *k) {
    if (k->type == CONSUMER) {
        pthread_create(&k->thread, NULL, nosdk_kafka_consumer_thread, k);
    } else if (k->type == PRODUCER) {
        pthread_create(&k->thread, NULL, nosdk_kafka_producer_thread, k);
    }
}

void nosdk_kafka_wait(struct nosdk_kafka *k) { pthread_join(k->thread, NULL); }

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
