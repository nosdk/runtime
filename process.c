#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
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

int nosdk_process_mgr_add(struct nosdk_process_mgr *mgr, char *command) {
    if (mgr->num_procs >= MAX_PROCS) {
        fprintf(stderr, "too many processes\n");
        return -1;
    }

    mgr->procs[mgr->num_procs].command = strdup(command);
    mgr->num_procs++;
    return 0;
}

int nosdk_process_start(
    struct nosdk_process_mgr *mgr, struct nosdk_process *proc) {

    printf("starting process: %s\n", proc->command);
    int stdout_pipe[2], stderr_pipe[2];

    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        perror("create pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        execl("/bin/sh", "sh", "-c", proc->command, NULL);
        perror("execl");
        exit(1);
    } else {
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        proc->pid = pid;
        proc->stdout_fd = stdout_pipe[0];
        proc->stderr_fd = stderr_pipe[0];
    }

    return 0;
}

void nosdk_process_mgr_start(struct nosdk_process_mgr *mgr) {
    if (mgr->num_procs == 0) {
        printf("no processes to run\n");
        exit(1);
    }

    for (int i = 0; i < mgr->num_procs; i++) {
        if (mgr->procs[i].pid == 0) {
            int ret = nosdk_process_start(mgr, &mgr->procs[i]);
            if (ret != 0) {
                exit(1);
            }
        }
    }

    int num_fds = mgr->num_procs * 2;

    struct pollfd *fds = malloc(sizeof(struct pollfd) * num_fds);
    memset(fds, 0, sizeof(struct pollfd) * num_fds);

    char *buf = malloc(1024);

    for (int i = 0; i < mgr->num_procs; i++) {
        fds[i].fd = mgr->procs[i].stdout_fd;
        fds[i].events = POLLIN;
        fds[i + mgr->num_procs].fd = mgr->procs[i].stderr_fd;
        fds[i + mgr->num_procs].events = POLLIN;
    }

    while (1) {
        int ready = poll(fds, num_fds, -1);

        if (ready > 0) {
            for (int i = 0; i < num_fds; i++) {
                if (fds[i].revents & POLLIN) {
                    ssize_t result = read(fds[i].fd, buf, 1024);
                    fprintf(stdout, "%.*s", (int)result, buf);
                }
            }
        }
    }
}
