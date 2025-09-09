#ifndef _NOSDK_HTTP_H
#define _NOSDK_HTTP_H

#define HEADER_BUF_SIZE 4096

struct nosdk_http_server {
    int socket_fd;
    int port;
    char *header_buf;
};

struct nosdk_http_server *nosdk_http_server_new();

int nosdk_http_handle(struct nosdk_http_server *server);

#endif // _NOSDK_HTTP_H
