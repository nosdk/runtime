#ifndef _NOSDK_PROCESS_H
#define _NOSDK_PROCESS_H

#include <unistd.h>

#define MAX_PROCS 100

struct nosdk_process {
    pid_t pid;
    int stdout_fd;
    int stderr_fd;
    char *command;
};

struct nosdk_process_mgr {
    struct nosdk_process procs[MAX_PROCS];
    int num_procs;
};

int nosdk_process_mgr_add(struct nosdk_process_mgr *mgr, char *command);

void nosdk_process_mgr_start(struct nosdk_process_mgr *mgr);

#endif // _NOSDK_PROCESS_H
