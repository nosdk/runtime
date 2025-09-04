#include <string.h>

#include "io.h"

int nosdk_io_mgr_init(struct nosdk_io_mgr *mgr) { return 0; }

int nosdk_io_mgr_add_kafka(struct nosdk_io_mgr *mgr, struct nosdk_kafka k) {
    int ret = nosdk_kafka_init(&k);
    if (ret != 0) {
        return ret;
    }
    mgr->kafkas[mgr->num_kafkas] = k;
    mgr->num_kafkas++;
    return 0;
}

int nosdk_io_mgr_kafka_subscribe(struct nosdk_io_mgr *mgr, char *topic) {
    for (int i = 0; i < mgr->num_kafkas; i++) {
        if (strcmp(mgr->kafkas[i].topic, topic) == 0) {
            return 0;
        }
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

int nosdk_io_mgr_start(struct nosdk_io_mgr *mgr) {

    for (int i = 0; i < mgr->num_kafkas; i++) {
        nosdk_kafka_start(&mgr->kafkas[i]);
    }

    return 0;
}
