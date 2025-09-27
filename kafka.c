#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <librdkafka/rdkafka.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "http.h"
#include "kafka.h"
#include "util.h"

struct nosdk_kafka_mgr *kafka_mgr;

int nosdk_kafka_mgr_init() {
    if (kafka_mgr != NULL) {
        return 0;
    }

    kafka_mgr = malloc(sizeof(struct nosdk_kafka_mgr));
    memset(kafka_mgr, 0, sizeof(struct nosdk_kafka_mgr));

    return 0;
}

int nosdk_kafka_mgr_add_kafka(
    struct nosdk_kafka_mgr *mgr, struct nosdk_kafka k) {
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

struct nosdk_kafka_thread_ctx *nosdk_kafka_mgr_make_thread(char *root_dir) {
    if (kafka_mgr->num_threads >= MAX_PROCS) {
        return NULL;
    }

    struct nosdk_kafka_thread_ctx *kthread =
        malloc(sizeof(struct nosdk_kafka_thread_ctx));
    kthread->root_dir = root_dir;
    kthread->thread = 0;
    kthread->k = NULL;

    kafka_mgr->threads[kafka_mgr->num_threads] = kthread;
    kafka_mgr->num_threads++;

    return kthread;
}

struct nosdk_kafka *nosdk_kafka_mgr_get_consumer(char *topic) {

    for (int i = 0; i < kafka_mgr->num_kafkas; i++) {
        if (strcmp(kafka_mgr->kafkas[i].topic, topic) == 0) {
            if (kafka_mgr->kafkas[i].type == CONSUMER) {
                return &kafka_mgr->kafkas[i];
            }
        }
    }

    return NULL;
}

int nosdk_kafka_mgr_kafka_subscribe(char *topic) {
    if (nosdk_kafka_mgr_get_consumer(topic) != NULL) {
        return 0;
    }

    struct nosdk_kafka k = {
        .type = CONSUMER,
        .topic = strdup(topic),
    };

    return nosdk_kafka_mgr_add_kafka(kafka_mgr, k);
}

struct nosdk_kafka *nosdk_kafka_mgr_get_producer() {
    for (int i = 0; i < kafka_mgr->num_kafkas; i++) {
        if (kafka_mgr->kafkas[i].type == PRODUCER) {
            return &kafka_mgr->kafkas[i];
        }
    }
    return NULL;
}

int nosdk_kafka_mgr_kafka_produce(char *topic) {
    if (nosdk_kafka_mgr_get_producer() != NULL) {
        return 0;
    }
    struct nosdk_kafka k = {
        .type = PRODUCER,
        .topic = strdup(topic),
    };

    return nosdk_kafka_mgr_add_kafka(kafka_mgr, k);
}

void kafka_conf_must_set(
    rd_kafka_conf_t *conf,
    const char *property,
    const char *envvar,
    char *default_value) {
    char err[512];
    char *value = getenv(envvar);
    if (value == NULL || strlen(value) == 0) {
        value = default_value;
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
        conf, "bootstrap.servers", "NOSDK_KAFKA_BOOTSTRAP_SERVERS",
        "0.0.0.0:19092");
    kafka_conf_must_set(
        conf, "group.id", "NOSDK_KAFKA_GROUP_ID", "nosdk-default-group");

    if (rd_kafka_conf_set(
            conf, "auto.offset.reset", "earliest", errstr, sizeof(errstr)) !=
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

    nosdk_debugf(
        "topic: %s partition: %d, offset: %" PRId64 "\n", consumer->topic,
        partition->partition, partition->offset);

    rd_kafka_resp_err_t err = rd_kafka_subscribe(consumer->rk, subscription);
    if (err) {
        fprintf(
            stderr, "failed to subscribe to %s: %s\n", consumer->topic,
            rd_kafka_err2str(err));
        return 1;
    }

    rd_kafka_topic_partition_list_destroy(subscription);
    return 0;
}

int nosdk_kafka_producer_init(struct nosdk_kafka *producer) {
    rd_kafka_conf_t *conf;
    char errstr[512];

    conf = rd_kafka_conf_new();

    kafka_conf_must_set(
        conf, "bootstrap.servers", "NOSDK_KAFKA_BOOTSTRAP_SERVERS",
        "0.0.0.0:19092");

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

char *nosdk_kafka_fifo_path(struct nosdk_kafka *k, char *root_dir) {
    char *buf = malloc(PATH_MAX);
    if (k->type == CONSUMER) {
        snprintf(buf, PATH_MAX, "%s/sub/%s", root_dir, k->topic);
    } else if (k->type == PRODUCER) {
        snprintf(buf, PATH_MAX, "%s/pub/%s", root_dir, k->topic);
    } else {
        return NULL;
    }
    return buf;
}

int nosdk_kafka_mkfifo(struct nosdk_kafka *k, char *root_dir) {
    char *path = nosdk_kafka_fifo_path(k, root_dir);

    struct stat st;
    if (stat(path, &st) == -1 && errno == ENOENT) {
        nosdk_debugf("creating %s\n", path);
        if (mkfifo(path, S_IRWXU) < 0) {
            perror("fifo creation");
            free(path);
            return -1;
        }
    }
    free(path);
    return 0;
}

void *nosdk_kafka_consumer_thread(void *arg) {
    struct nosdk_kafka_thread_ctx *ctx = (struct nosdk_kafka_thread_ctx *)arg;
    nosdk_debugf(
        "starting consumer thread for topic %s in %s\n", ctx->k->topic,
        ctx->root_dir);

    // ignore SIGPIPE so we can handle it
    signal(SIGPIPE, SIG_IGN);

    if (nosdk_kafka_mkfifo(ctx->k, ctx->root_dir) < 0) {
        return NULL;
    }

    char *fifo_path = nosdk_kafka_fifo_path(ctx->k, ctx->root_dir);

    while (1) {
        nosdk_debugf("%s: waiting for reader\n ", ctx->root_dir);

        int write_fd = open(fifo_path, O_WRONLY);
        if (write_fd < 0) {
            perror("opening fifo");
            break;
        }

        rd_kafka_message_t *msg;
        int no_message = 1;

        while (no_message) {
            nosdk_debugf(
                "%s: reading connected, polling %s\n", ctx->root_dir,
                ctx->k->topic);
            msg = rd_kafka_consumer_poll(ctx->k->rk, 500);
            no_message = (msg == NULL);
        }

        if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            printf("poll error: %s\n", rd_kafka_err2str(msg->err));
            rd_kafka_message_destroy(msg);
            close(write_fd);
            continue;
        }

        // write message to pipe
        int total_written = 0;

        while (total_written < msg->len) {
            ssize_t result = write(
                write_fd, (char *)msg->payload + total_written,
                msg->len - total_written);
            total_written += result;
        }

        // write headers
        nosdk_kafka_write_headers(msg, fifo_path);

        fsync(write_fd);
        close(write_fd);

        // commit and destroy
        rd_kafka_resp_err_t commit_err =
            rd_kafka_commit_message(ctx->k->rk, msg, 1);
        if (commit_err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            printf("commit error: %s\n", rd_kafka_err2str(commit_err));
        }
        rd_kafka_message_destroy(msg);

        usleep(3000);
    }

    return NULL;
}

void *nosdk_kafka_producer_thread(void *arg) {
    struct nosdk_kafka_thread_ctx *ctx = (struct nosdk_kafka_thread_ctx *)arg;
    nosdk_debugf(
        "starting producer thread for topic %s in %s\n", ctx->k->topic,
        ctx->root_dir);

    if (nosdk_kafka_mkfifo(ctx->k, ctx->root_dir) < 0) {
        return NULL;
    }

    // kafka max message size is 1mb!
    char *msg_buf = malloc(1000 * 1000);

    char *fifo_path = nosdk_kafka_fifo_path(ctx->k, ctx->root_dir);

    int read_fd = open(fifo_path, O_RDONLY | O_NONBLOCK);
    if (read_fd < 0) {
        perror("opening fifo");
        free(msg_buf);
        free(fifo_path);
        return NULL;
    }

    struct pollfd pfd[1] = {
        {
            .fd = read_fd,
            .events = POLLIN | POLLHUP,
        },
    };

    while (1) {
        int ready = poll(pfd, 1, -1);
        nosdk_debugf("producer: %d poll fds are ready\n", ready);

        if (ready == -1) {
            perror("poll error");
            break;
        } else if (ready == 0) {
            continue;
        }

        if (pfd[0].revents & POLLIN) {
            ssize_t result = read(read_fd, msg_buf, 1000 * 1000);

            nosdk_debugf("producer read data: %.*s\n", (int)result, msg_buf);

            if (result == 0) {
                continue;
            }

            rd_kafka_producev(
                ctx->k->rk, RD_KAFKA_V_TOPIC(ctx->k->topic),
                RD_KAFKA_V_VALUE(msg_buf, result), RD_KAFKA_V_END);
        } else if (pfd[0].revents & POLLHUP || pfd[0].revents & POLLERR) {
            printf("process hung up\n");
            // this never happens...
        }
    }

    close(read_fd);

    rd_kafka_flush(ctx->k->rk, 5000);
    free(msg_buf);
    free(fifo_path);
    return NULL;
}

char *get_topic_name(struct nosdk_http_request *req) {
    char *topic_prefix = "/sub/";
    char *name = strdup(&req->path[strlen(topic_prefix)]);
    int len = strlen(name);

    for (int i = 0; i < len; i++) {
        name[i] = tolower(name[i]);
        if (name[i] == '?' || name[i] == '/') {
            name[i] = '\0';
        }
    }
    return name;
}

void nosdk_kafka_sub_handler(struct nosdk_http_request *req) {
    char *topic_name = get_topic_name(req);
    struct nosdk_kafka *consumer = nosdk_kafka_mgr_get_consumer(topic_name);
    if (consumer == NULL) {
        free(topic_name);
        nosdk_http_respond(
            req, HTTP_STATUS_INTERNAL_ERROR, "text/plain", NULL, 0);
        return;
    }

    rd_kafka_message_t *msg = NULL;
    for (int i = 0; i < 20 && msg == NULL; i++) {
        msg = rd_kafka_consumer_poll(consumer->rk, 500);
    }
    if (msg == NULL) {
        free(topic_name);
        nosdk_http_respond(req, HTTP_STATUS_OK, "application/json", "null", 4);
        return;
    }

    if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        printf("poll error: %s\n", rd_kafka_err2str(msg->err));
        rd_kafka_message_destroy(msg);
        free(topic_name);
        nosdk_http_respond(
            req, HTTP_STATUS_INTERNAL_ERROR, "text/plain", NULL, 0);
        return;
    }

    nosdk_http_respond(
        req, HTTP_STATUS_OK, "application/json", (char *)msg->payload,
        msg->len);

    rd_kafka_message_destroy(msg);
    free(topic_name);
}

void nosdk_kafka_pub_handler(struct nosdk_http_request *req) {
    char *topic_name = get_topic_name(req);
    struct nosdk_kafka *producer = nosdk_kafka_mgr_get_producer();
    if (producer == NULL) {
        free(topic_name);
        nosdk_http_respond(
            req, HTTP_STATUS_INTERNAL_ERROR, "text/plain", NULL, 0);
        return;
    }

    char *body_data = nosdk_http_request_body_alloc(req);

    rd_kafka_resp_err_t resp;

    resp = rd_kafka_producev(
        producer->rk, RD_KAFKA_V_TOPIC(topic_name),
        RD_KAFKA_V_VALUE(body_data, strlen(body_data)), RD_KAFKA_V_END);

    if (resp != RD_KAFKA_RESP_ERR_NO_ERROR) {
        const char *err = rd_kafka_err2str(resp);
        printf("producer error: %s\n", err);
        free(topic_name);
        free(body_data);
        nosdk_http_respond(
            req, HTTP_STATUS_INTERNAL_ERROR, "text/plain", (char *)err,
            strlen(err));
        return;
    }

    resp = rd_kafka_flush(producer->rk, 500);

    if (resp != RD_KAFKA_RESP_ERR_NO_ERROR) {
        const char *err = rd_kafka_err2str(resp);
        printf("producer flush error: %s\n", err);
        free(topic_name);
        free(body_data);
        nosdk_http_respond(
            req, HTTP_STATUS_INTERNAL_ERROR, "text/plain", (char *)err,
            strlen(err));
        return;
    }

    free(topic_name);
    free(body_data);
    nosdk_http_respond(req, HTTP_STATUS_OK, "text/plain", NULL, 0);
}

void nosdk_kafka_mgr_teardown() {
    for (int i = 0; i < kafka_mgr->num_kafkas; i++) {
        nosdk_debugf("destroying kafka client %d\n", i);
        if (kafka_mgr->kafkas[i].type == PRODUCER) {
            rd_kafka_flush(kafka_mgr->kafkas[i].rk, 500);
        }
        rd_kafka_destroy(kafka_mgr->kafkas[i].rk);
    }

    for (int i = 0; i < kafka_mgr->num_threads; i++) {
        free(kafka_mgr->threads[i]);
    }
}
