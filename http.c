#include "http.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    const char *name;
    http_method_t method;
} method_map_t;

static const method_map_t method_table[] = {
    {"GET", HTTP_METHOD_GET},         {"POST", HTTP_METHOD_POST},
    {"PUT", HTTP_METHOD_PUT},         {"DELETE", HTTP_METHOD_DELETE},
    {"HEAD", HTTP_METHOD_HEAD},       {"OPTIONS", HTTP_METHOD_OPTIONS},
    {"PATCH", HTTP_METHOD_PATCH},     {"TRACE", HTTP_METHOD_TRACE},
    {"CONNECT", HTTP_METHOD_CONNECT}, {NULL, HTTP_METHOD_UNKNOWN} // Sentinel
};

http_method_t nosdk_parse_method(char *data, int len) {
    for (int i = 0; method_table[i].name != NULL; i++) {
        if (strlen(method_table[i].name) == len) {
            if (memcmp(data, method_table[i].name, len) == 0) {
                return method_table[i].method;
            }
        }
    }
    return HTTP_METHOD_UNKNOWN;
}

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

void consume_header(struct nosdk_http_request *req, char *name, char *value) {
    if (strncasecmp(name, "content-length", strlen(name)) == 0) {
        req->content_length = atoi(value);
    }
}

char *nosdk_http_request_body_alloc(struct nosdk_http_request *req) {
    char *data = malloc(req->content_length + 1);
    int data_pos = 0;

    if (req->body_data_len != 0) {
        memcpy(data, req->body_data, req->body_data_len);
        data_pos = req->body_data_len;
    }

    while (data_pos < req->content_length) {
        ssize_t result = read(
            req->client_fd, &data[req->body_data_len],
            req->content_length - req->body_data_len);

        data_pos += result;
    }

    data[req->content_length] = '\0';

    return data;
}

struct nosdk_http_request *
nosdk_http_parse_head(struct nosdk_http_server *server, int client_fd) {

    struct nosdk_http_request *req = malloc(sizeof(struct nosdk_http_request));
    req->client_fd = client_fd;

    ssize_t result =
        recv(req->client_fd, server->header_buf, HEADER_BUF_SIZE, 0);

    int path_start = 0;
    int path_len = 0;

    // method and path
    for (int i = 0; i < result; i++) {
        if (server->header_buf[i] == ' ' && path_start == 0) {
            req->method = nosdk_parse_method(server->header_buf, i);

            path_start = i + 1;
        } else if (
            server->header_buf[i] == ' ' && path_start != 0 && path_len == 0) {
            path_len = i - path_start;
            break;
        }
    }

    memcpy(req->path, &server->header_buf[path_start], path_len);
    req->path[path_len] = '\0';

    // headers
    char last_char = server->header_buf[path_start + path_len];
    int in_header = 0;
    int in_value = 0;
    char name_buf[128];
    int name_buf_pos = 0;
    char value_buf[128];
    int value_buf_pos = 0;
    int consecutive_rns = 0;

    for (int i = path_start + path_len; i < result; i++) {
        char this_char = server->header_buf[i];

        if (in_header) {
            if (!in_value) {
                if (this_char == ':') {
                    in_value = 1;
                }
                name_buf[name_buf_pos] = last_char;
                name_buf_pos++;
            } else {
                if (this_char != '\r') {
                    if (this_char != ' ') {
                        value_buf[value_buf_pos] = this_char;
                        value_buf_pos++;
                    }
                } else {
                    // end of header
                    //
                    name_buf[name_buf_pos] = '\0';
                    value_buf[value_buf_pos] = '\0';
                    consume_header(req, name_buf, value_buf);
                    in_header = 0;
                    in_value = 0;
                    name_buf_pos = 0;
                    value_buf_pos = 0;
                }
            }
        }

        if (last_char == '\n' && !in_header) {
            in_header = 1;
        }

        if (last_char == '\r' || last_char == '\n') {
            consecutive_rns += 1;
        } else {
            consecutive_rns = 0;
        }

        if (consecutive_rns == 4) {
            printf("headers end at byte %d/%d\n", i, (int)result);
            memcpy(req->body_data, &server->header_buf[i], result - i);
            req->body_data_len = result - i;
            break;
        }

        last_char = this_char;
    }

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
