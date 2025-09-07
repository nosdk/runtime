#ifndef _NOSDK_CONFIG_H
#define _NOSDK_CONFIG_H

enum nosdk_messaging_interface {
    FS,
    HTTP,
};

struct nosdk_messaging_config {
    char *topic;
    enum nosdk_messaging_interface interface;
};

struct nosdk_process_config {
    char *name;
    char *command;
    int nproc;
    struct nosdk_messaging_config *consume;
    unsigned consume_count;
    struct nosdk_messaging_config *produce;
    unsigned produce_count;
};

struct nosdk_config {
    struct nosdk_process_config *processes;
    unsigned processes_count;
};

int nosdk_config_load(char *filepath, struct nosdk_config **config);

#endif // _NOSDK_CONFIG_H
