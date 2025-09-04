#include "kafka.c"

int main(int argc, char *argv[]) {
    struct nosdk_kafka consumer = {
        .type = CONSUMER,
        .topic = "records",
    };

    struct nosdk_kafka producer = {
        .type = PRODUCER,
        .topic = "stuff",
    };

    int ret = nosdk_kafka_init(&consumer);
    if (ret != 0) {
        return ret;
    }

    ret = nosdk_kafka_init(&producer);
    if (ret != 0) {
        return ret;
    }

    nosdk_kafka_start(&consumer);
    nosdk_kafka_start(&producer);

    nosdk_kafka_wait(&consumer);
    nosdk_kafka_wait(&producer);

    return 0;
}
