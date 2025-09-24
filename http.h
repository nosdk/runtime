#ifndef _NOSDK_HTTP_H
#define _NOSDK_HTTP_H

#define HEADER_BUF_SIZE 4096
#define MAX_HANDLERS 16
#define HTTP_PATH_MAX 256

typedef enum {
    HTTP_METHOD_UNKNOWN = 0,
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_PATCH,
    HTTP_METHOD_TRACE,
    HTTP_METHOD_CONNECT
} http_method_t;

typedef enum {
    HTTP_STATUS_NONE = 0,
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_NO_CONTENT = 204,
    HTTP_STATUS_INVALID_REQUEST = 400,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_INTERNAL_ERROR = 500,
} http_status_t;

struct nosdk_http_request {
    http_method_t method;
    char path[HTTP_PATH_MAX];
    int content_length;

    char body_data[HTTP_PATH_MAX];
    int body_data_len;

    int client_fd;
};

char *nosdk_http_request_body_alloc(struct nosdk_http_request *req);

int nosdk_http_respond(
    struct nosdk_http_request *req,
    http_status_t status,
    char *content_type,
    char *body,
    int body_len);

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
