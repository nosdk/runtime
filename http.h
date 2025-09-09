#ifndef _NOSDK_HTTP_H
#define _NOSDK_HTTP_H

#define HEADER_BUF_SIZE 4096
#define MAX_HANDLERS 16
#define HTTP_PATH_MAX 256

struct nosdk_http_request {
    char path[HTTP_PATH_MAX];
    int client_fd;
};

struct nosdk_http_handler {
    char *prefix;
    void (*handler)(struct nosdk_http_request *req);
};

struct nosdk_http_server {
    int socket_fd;
    int port;
    char *header_buf;

    struct nosdk_http_handler handlers[MAX_HANDLERS];
    int num_handlers;
};

struct nosdk_http_server *nosdk_http_server_new();

int nosdk_http_server_handle(
    struct nosdk_http_server *server, struct nosdk_http_handler handler);

int nosdk_http_server_start(struct nosdk_http_server *server);

#endif // _NOSDK_HTTP_H
