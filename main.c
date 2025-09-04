#include "kafka.c"

int main(int argc, char *argv[]) {
    struct nosdk_kafka_consumer consumer = {
        .topic = "records",
    };

    int ret = nosdk_kafka_consumer_init(&consumer);
    if (ret != 0) {
        return ret;
    }

    nosdk_kafka_consumer_start(&consumer);

    nosdk_kafka_consumer_wait(&consumer);

    return 0;
}
