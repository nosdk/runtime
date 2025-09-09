#ifndef _NOSDK_PROCESS_H
#define _NOSDK_PROCESS_H

#include <unistd.h>

#include "io.h"

#define MAX_IO 16

struct nosdk_process {
    char *name;
    char *command;
    char *root_dir;

    pid_t pid;
    int stdout_fd;
    int stderr_fd;
    char *output_buf;

    // io
    struct nosdk_io_spec io[MAX_IO];
    int num_io;
};

void nosdk_process_add_io(
    struct nosdk_process *proc, struct nosdk_io_spec spec);

struct nosdk_process_mgr {
    struct nosdk_io_mgr *io_mgr;
    struct nosdk_process procs[MAX_PROCS];
    int num_procs;
};

int nosdk_process_mgr_add(
    struct nosdk_process_mgr *mgr, struct nosdk_process proc);

void nosdk_process_mgr_start(struct nosdk_process_mgr *mgr);

#endif // _NOSDK_PROCESS_H
