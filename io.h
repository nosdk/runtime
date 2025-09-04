#ifndef _NOSDK_IO_H
#define _NOSDK_IO_H

#define MAX_KAFKA 16

#include "kafka.h"

struct nosdk_io_mgr {
    struct nosdk_kafka kafkas[MAX_KAFKA];
    int num_kafkas;
};

int nosdk_io_mgr_init(struct nosdk_io_mgr *mgr);

// Add a kafka subscription. If there is already a consumer for the topic,
// does nothing
int nosdk_io_mgr_kafka_subscribe(struct nosdk_io_mgr *mgr, char *topic);

// Add a kafka producer
int nosdk_io_mgr_kafka_produce(struct nosdk_io_mgr *mgr, char *topic);

int nosdk_io_mgr_start(struct nosdk_io_mgr *mgr);

#endif // _NOSDK_IO_H
