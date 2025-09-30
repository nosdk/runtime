#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "io.h"
#include "process.h"
#include "util.h"

#define OUTPUT_BUF_SIZE 1024

int should_run = 1;

void nosdk_process_mgr_stop(int n) { should_run = 0; }

int nosdk_process_mgr_add(
    struct nosdk_process_mgr *mgr, struct nosdk_process proc) {
    if (mgr->num_procs >= MAX_PROCS) {
        fprintf(stderr, "too many processes\n");
        return -1;
    }

    if (proc.output_buf == NULL) {
        proc.output_buf = malloc(OUTPUT_BUF_SIZE);
    }

    if (proc.name == NULL) {
        // use the first word of command as the name
        proc.name = strdup(proc.command);
        int clen = strlen(proc.command);
        for (int i = 0; i < clen; i++) {
            if (proc.command[i] == ' ') {
                proc.name[i] = '\0';
                break;
            }
        }
    }

    mgr->procs[mgr->num_procs] = proc;
    mgr->num_procs++;

    return 0;
}

void nosdk_process_mgr_destroy(struct nosdk_process_mgr *mgr) {
    for (int i = 0; i < mgr->num_procs; i++) {
        if (mgr->procs[i].pid != -1) {
            kill(mgr->procs[i].pid, SIGTERM);
            waitpid(mgr->procs[i].pid, NULL, 0);
            nosdk_debugf("stopped %d\n", mgr->procs[i].pid);
        }

        if (mgr->procs[i].ctx->root_dir) {
            nosdk_debugf("removing %s\n", mgr->procs[i].ctx->root_dir);

            // hacky, but does the right thing
            pid_t pid = fork();
            if (pid == 0) {
                char *argv[] = {"rm", "-rf", mgr->procs[i].ctx->root_dir, NULL};
                execvp("rm", argv);
            } else {
                waitpid(pid, NULL, 0);
            }
            free(mgr->procs[i].ctx->root_dir);
        }

        free(mgr->procs[i].output_buf);
    }
    nosdk_debugf("process manager destroy finished\n");
}

char *nosdk_process_mgr_mkenv(
    struct nosdk_process_mgr *mgr, struct nosdk_process *proc) {

    DIR *dir;
    struct dirent *entry;
    char root_dir[] = "/tmp/nosdk-XXXXXX";
    char cwd[PATH_MAX];
    char src_path[PATH_MAX];
    char dst_path[PATH_MAX];

    if (mkdtemp(root_dir) == NULL) {
        perror("creating temp dir");
        return NULL;
    }

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        return NULL;
    }

    dir = opendir(cwd);
    if (dir == NULL) {
        perror("opendir");
        return NULL;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        int src_len = snprintf(src_path, PATH_MAX, "%s/%s", cwd, entry->d_name);
        int dst_len =
            snprintf(dst_path, PATH_MAX, "%s/%s", root_dir, entry->d_name);
        if (src_len >= PATH_MAX || dst_len >= PATH_MAX) {
            fprintf(
                stderr, "path too long: %s or %s\n", entry->d_name, root_dir);
            closedir(dir);
            return NULL;
        }

        if (symlink(src_path, dst_path) == -1) {
            fprintf(
                stderr, "error creating symlink %s: %s\n", dst_path,
                strerror(errno));
            return NULL;
        }
    }

    // create special directories
    snprintf(dst_path, PATH_MAX, "%s/pub", root_dir);
    if (mkdir(dst_path, 0755) < 0) {
        perror("make pub dir");
        return NULL;
    }
    snprintf(dst_path, PATH_MAX, "%s/sub", root_dir);
    if (mkdir(dst_path, 0755) < 0) {
        perror("make sub dir");
        return NULL;
    }

    closedir(dir);
    return strdup(root_dir);
}

int nosdk_process_start(
    struct nosdk_process_mgr *mgr, struct nosdk_process *proc) {

    nosdk_debugf("starting process: %s\n", proc->command);
    int stdout_pipe[2], stderr_pipe[2];

    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        perror("create pipe");
        return -1;
    }

    proc->ctx = nosdk_io_process_ctx_new(mgr->io_mgr);

    proc->ctx->root_dir = nosdk_process_mgr_mkenv(mgr, proc);
    if (proc->ctx->root_dir == NULL) {
        return -1;
    }

    for (int i = 0; i < proc->num_io; i++) {
        int ret = nosdk_io_mgr_setup(mgr->io_mgr, proc->ctx, proc->io[i]);
        if (ret == -1) {
            return -1;
        }
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {

        if (mgr->num_procs > 1) {
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);

            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);

            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
        }

        if (chdir(proc->ctx->root_dir) < 0) {
            perror("chdir");
            exit(1);
        }

        // set http environment variable
        char env_buf[128];
        snprintf(
            env_buf, sizeof(env_buf), "http://localhost:%d",
            proc->ctx->server->port);
        setenv("NOSDK", env_buf, 1);

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

void nosdk_process_mgr_print_output(
    struct nosdk_process *proc, int fd, int is_stdout) {

    ssize_t bytes_read = read(fd, proc->output_buf, OUTPUT_BUF_SIZE - 1);
    if (bytes_read <= 0)
        return;

    int line_start = 0;
    FILE *output = is_stdout ? stdout : stderr;

    for (int i = 0; i < bytes_read; i++) {
        if (proc->output_buf[i] == '\n') {
            // Print complete line (excluding the newline)
            fprintf(
                output, "[%s] %.*s\n", proc->name, i - line_start,
                &proc->output_buf[line_start]);
            line_start = i + 1;
        }
    }

    // Print any remaining partial line without newline
    if (line_start < bytes_read) {
        fprintf(
            output, "[%s] %.*s", proc->name, (int)(bytes_read - line_start),
            &proc->output_buf[line_start]);
    }
}

void nosdk_process_mgr_start(struct nosdk_process_mgr *mgr) {
    if (mgr->num_procs == 0) {
        printf("no processes to run\n");
        exit(1);
    }

    signal(SIGINT, nosdk_process_mgr_stop);

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

    for (int i = 0; i < mgr->num_procs; i++) {
        fds[i].fd = mgr->procs[i].stdout_fd;
        fds[i].events = POLLIN;
        fds[i + mgr->num_procs].fd = mgr->procs[i].stderr_fd;
        fds[i + mgr->num_procs].events = POLLIN;
    }

    int active_procs = mgr->num_procs;

    nosdk_io_mgr_start(mgr->io_mgr);

    while (should_run && active_procs > 0) {
        int ready = poll(fds, num_fds, -1);

        if (ready > 0) {
            for (int i = 0; i < num_fds; i++) {
                int proc_idx = i < mgr->num_procs ? i : i - mgr->num_procs;
                int is_stdout = (i < mgr->num_procs);

                if (fds[i].revents & POLLIN) {
                    nosdk_process_mgr_print_output(
                        &mgr->procs[proc_idx], fds[i].fd, is_stdout);
                }

                if (fds[i].revents & (POLLHUP | POLLERR)) {

                    if (mgr->procs[proc_idx].pid != -1) {
                        int status;
                        pid_t result =
                            waitpid(mgr->procs[proc_idx].pid, &status, WNOHANG);

                        if (result > 0) {
                            printf(
                                "process %d exited with status %d\n",
                                mgr->procs[proc_idx].pid, WEXITSTATUS(status));
                            mgr->procs[proc_idx].pid = -1;
                            active_procs--;

                            close(mgr->procs[proc_idx].stdout_fd);
                            close(mgr->procs[proc_idx].stderr_fd);
                            fds[proc_idx].fd = -1;
                            fds[proc_idx + mgr->num_procs].fd = -1;
                        }
                    }
                }
            }
        }
    }

    nosdk_process_mgr_destroy(mgr);
    free(fds);
}

void nosdk_process_add_io(
    struct nosdk_process *proc, struct nosdk_io_spec spec) {

    proc->io[proc->num_io] = spec;
    proc->num_io++;
}
