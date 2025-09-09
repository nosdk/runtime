#include "http.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_types/_ssize_t.h>
#include <sys/socket.h>
#include <unistd.h>

struct nosdk_http_server *nosdk_http_server_new() {
    int opt = 1;

    struct sockaddr_in addr = {
        .sin_addr = {.s_addr = INADDR_ANY},
        .sin_port = htons(0),
        .sin_family = AF_INET,
    };

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!socket_fd) {
        perror("socket create");
        return NULL;
    }

    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        return NULL;
    }

    if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return NULL;
    }

    socklen_t addr_len = sizeof(addr);
    if (getsockname(socket_fd, (struct sockaddr *)&addr, &addr_len) == -1) {
        perror("getsockname");
        return NULL;
    }

    struct nosdk_http_server *server = malloc(sizeof(struct nosdk_http_server));
    server->socket_fd = socket_fd;
    server->port = ntohs(addr.sin_port);
    server->header_buf = malloc(HEADER_BUF_SIZE);
    server->num_handlers = 0;

    return server;
}

int nosdk_http_server_handle(
    struct nosdk_http_server *server, struct nosdk_http_handler handler) {

    if (server->num_handlers >= MAX_HANDLERS) {
        fprintf(stderr, "http server max handlers reached\n");
        return -1;
    }

    server->handlers[server->num_handlers] = handler;
    server->num_handlers++;

    return 0;
}

struct nosdk_http_request *
nosdk_http_parse_head(struct nosdk_http_server *server, int client_fd) {

    struct nosdk_http_request *req = malloc(sizeof(struct nosdk_http_request));
    req->client_fd = client_fd;

    ssize_t result = read(req->client_fd, server->header_buf, 1024);

    int path_start = 0;
    int path_len = 0;

    for (int i = 0; i < result; i++) {
        if (server->header_buf[i] == ' ' && path_start == 0) {
            path_start = i + 1;
        } else if (server->header_buf[i] == ' ' && path_start != 0) {
            path_len = i - path_start;
            break;
        }
    }

    memcpy(req->path, &server->header_buf[path_start], path_len);
    req->path[path_len] = '\0';

    return req;
}

void nosdk_http_request_end(struct nosdk_http_request *req) {
    close(req->client_fd);
    free(req);
}

void nosdk_http_respond_not_found(struct nosdk_http_request *req) {
    char *response = "HTTP/1.1 404 Not Found";
    write(req->client_fd, response, strlen(response));
    nosdk_http_request_end(req);
}

void nosdk_http_respond_invalid(int client_fd) {
    char *response = "HTTP/1.1 400 Invalid Request\nContent-Length: 0";
    write(client_fd, response, strlen(response));
    close(client_fd);
}

int nosdk_http_handle(struct nosdk_http_server *server) {
    struct sockaddr_in client_addr = {0};
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(server->socket_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        return -1;
    }

    struct nosdk_http_request *req = nosdk_http_parse_head(server, client_fd);
    if (req == NULL) {
        nosdk_http_respond_invalid(client_fd);
        return 0;
    }

    printf("request path: %s\n", req->path);

    for (int i = 0; i < server->num_handlers; i++) {
        struct nosdk_http_handler *handler = &server->handlers[i];

        if (memcmp(req->path, handler->prefix, strlen(handler->prefix)) == 0) {
            handler->handler(req);
            nosdk_http_request_end(req);
            return 0;
        }
    }

    nosdk_http_respond_not_found(req);
    return 0;
}

int nosdk_http_server_start(struct nosdk_http_server *server) {
    if (listen(server->socket_fd, 10) != 0) {
        perror("listen");
        return -1;
    }

    while (1) {
        if (nosdk_http_handle(server) != 0) {
            break;
        }
    }

    return 0;
}
