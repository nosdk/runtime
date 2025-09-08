#include <errno.h>
#include <fcntl.h>
#include <librdkafka/rdkafka.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "io.h"
#include "util.h"

int nosdk_io_mgr_init(struct nosdk_io_mgr *mgr) { return 0; }

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

void nosdk_io_mgr_add_kafka_ctx(
    struct nosdk_io_mgr *mgr, struct nosdk_kafka_thread_ctx *ctx) {
    mgr->kafka_contexts[mgr->num_kafka_contexts] = ctx;
    mgr->num_kafka_contexts++;
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
                write_fd, &msg->payload[total_written],
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

int nosdk_io_mgr_setup(
    struct nosdk_io_mgr *mgr, struct nosdk_io_spec spec, char *root_dir) {

    int ret;

    struct nosdk_kafka_thread_ctx *ctx =
        malloc(sizeof(struct nosdk_kafka_thread_ctx));
    ctx->root_dir = root_dir;
    ctx->thread = 0;
    ctx->k = NULL;

    if (spec.kind == KAFKA_CONSUME_TOPIC) {
        ret = nosdk_io_mgr_kafka_subscribe(mgr, spec.data);
        if (ret != 0) {
            return ret;
        }

        ctx->k = nosdk_io_mgr_get_consumer(mgr, spec.data);
        pthread_create(&ctx->thread, NULL, nosdk_kafka_consumer_thread, ctx);

    } else if (spec.kind == KAFKA_PRODUCE_TOPIC) {
        ret = nosdk_io_mgr_kafka_produce(mgr, spec.data);
        if (ret != 0) {
            return ret;
        }

        ctx->k = nosdk_io_mgr_get_producer(mgr);
        pthread_create(&ctx->thread, NULL, nosdk_kafka_producer_thread, ctx);
    }

    if (ctx->k == NULL) {
        return -1;
    }

    nosdk_io_mgr_add_kafka_ctx(mgr, ctx);
    return 0;
}

void nosdk_io_mgr_teardown(struct nosdk_io_mgr *mgr) {
    for (int i = 0; i < mgr->num_kafkas; i++) {
        if (mgr->kafkas[i].type == PRODUCER) {
            rd_kafka_flush(mgr->kafkas[i].rk, 500);
        }
        nosdk_debugf("destroying kafka %s\n", mgr->kafkas[i].type);
        rd_kafka_destroy(mgr->kafkas[i].rk);
    }
}
