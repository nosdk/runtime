#include <cyaml/cyaml.h>
#include <stdlib.h>

#include "config.h"

static const cyaml_strval_t nosdk_messaging_interface_strings[] = {
    {"fs", FS},
    {"http", HTTP},
};

static const cyaml_schema_field_t nosdk_messaging_config_schema[] = {
    CYAML_FIELD_STRING_PTR(
        "topic",
        CYAML_FLAG_POINTER,
        struct nosdk_messaging_config,
        topic,
        0,
        CYAML_UNLIMITED),
    CYAML_FIELD_ENUM(
        "interface",
        CYAML_FLAG_DEFAULT,
        struct nosdk_messaging_config,
        interface,
        nosdk_messaging_interface_strings,
        2),
    CYAML_FIELD_END};

static const cyaml_schema_value_t nosdk_messaging_config_schema_value = {
    CYAML_VALUE_MAPPING(
        CYAML_FLAG_DEFAULT,
        struct nosdk_messaging_config,
        nosdk_messaging_config_schema),
};

static const cyaml_schema_field_t nosdk_process_config_schema[] = {
    CYAML_FIELD_STRING_PTR(
        "name",
        CYAML_FLAG_POINTER,
        struct nosdk_process_config,
        name,
        0,
        CYAML_UNLIMITED),
    CYAML_FIELD_STRING_PTR(
        "command",
        CYAML_FLAG_POINTER,
        struct nosdk_process_config,
        command,
        0,
        CYAML_UNLIMITED),
    CYAML_FIELD_INT(
        "nproc", CYAML_FLAG_OPTIONAL, struct nosdk_process_config, nproc),
    CYAML_FIELD_SEQUENCE(
        "consume",
        CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
        struct nosdk_process_config,
        consume,
        &nosdk_messaging_config_schema_value,
        0,
        CYAML_UNLIMITED),
    CYAML_FIELD_SEQUENCE(
        "produce",
        CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
        struct nosdk_process_config,
        produce,
        &nosdk_messaging_config_schema_value,
        0,
        CYAML_UNLIMITED),
    CYAML_FIELD_END};

static const cyaml_schema_value_t nosdk_process_config_schema_value = {
    CYAML_VALUE_MAPPING(
        CYAML_FLAG_DEFAULT,
        struct nosdk_process_config,
        nosdk_process_config_schema),
};

static const cyaml_schema_field_t nosdk_config_schema[] = {
    CYAML_FIELD_SEQUENCE(
        "processes",
        CYAML_FLAG_POINTER,
        struct nosdk_config,
        processes,
        &nosdk_process_config_schema_value,
        0,
        CYAML_UNLIMITED),
    CYAML_FIELD_END};

static const cyaml_schema_value_t nosdk_config_schema_value = {
    CYAML_VALUE_MAPPING(
        CYAML_FLAG_POINTER, struct nosdk_config, nosdk_config_schema),
};

static const cyaml_config_t cyaml_config = {
    .log_fn = cyaml_log,
    .mem_fn = cyaml_mem,
    .log_level = CYAML_LOG_WARNING,
};

int nosdk_config_load(char *filepath, struct nosdk_config **config) {
    cyaml_err_t err;

    err = cyaml_load_file(
        filepath, &cyaml_config, &nosdk_config_schema_value,
        (cyaml_data_t **)config, NULL);
    if (err != CYAML_OK) {
        return -1;
    }

    return 0;
}

void nosdk_config_destroy(struct nosdk_config *config) {
    for (int i = 0; i < config->processes_count; i++) {
        free(config->processes[i].consume);
        free(config->processes[i].produce);
        free(config->processes[i].command);
    }
}
