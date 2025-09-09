#include <libpq-fe.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "http.h"
#include "postgres.h"
#include "util.h"

void nosdk_pg_disconnect(PGconn *conn) { PQfinish(conn); }

PGconn *nosdk_pg_connect() {
    PGconn *conn = PQconnectdb(
        "dbname=nosdk user=nosdk password=nosdk host=localhost port=15432");
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "connection error: %s\n", PQerrorMessage(conn));
        nosdk_pg_disconnect(conn);
        return NULL;
    }

    return conn;
}

int table_exists(PGconn *conn, const char *table_name) {
    const char *query = "SELECT EXISTS ("
                        "SELECT 1 FROM information_schema.tables "
                        "WHERE table_schema = 'public' AND table_name = $1"
                        ")";

    const char *params[1] = {table_name};
    PGresult *res = PQexecParams(conn, query, 1, NULL, params, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Query failed: %s", PQerrorMessage(conn));
        PQclear(res);
        return -1;
    }

    int exists = 0;
    if (PQntuples(res) > 0) {
        char *result = PQgetvalue(res, 0, 0);
        exists = (result[0] == 't');
    }

    PQclear(res);
    return exists;
}

int create_table_jsonb(PGconn *conn, const char *table_name) {
    char query[256];
    snprintf(
        query, sizeof(query),
        ("CREATE TABLE %s ("
         "id SERIAL PRIMARY KEY,"
         "data JSONB NOT NULL)"),
        table_name);

    PGresult *res = PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "insert failed: %s\n", PQerrorMessage(conn));
        PQclear(res);
        return -1;
    }

    PQclear(res);
    return 0;
}

char *get_table_name(struct nosdk_http_request *req) {
    char *table_prefix = "/db/tables";
    return &req->path[strlen(table_prefix) + 1];
}

void nosdk_pg_handle_post(struct nosdk_http_request *req, PGconn *conn) {
    char *data = nosdk_http_request_body_alloc(req);

    char *table_name = get_table_name(req);
    if (!table_exists(conn, table_name)) {
        if (create_table_jsonb(conn, table_name) != 0) {
            char *response = "HTTP/1.1 500 Internal Error";
            write(req->client_fd, response, strlen(response));
            return;
        }
    }

    struct nosdk_string_buffer *sb = nosdk_string_buffer_new();

    nosdk_string_buffer_append(
        sb, "INSERT INTO %s (data) VALUES ('%s'::jsonb)", table_name, data);

    sb->data[sb->size] = '\0';

    printf("statement: %.*s\n", sb->size, sb->data);

    PGresult *res = PQexec(conn, sb->data);
    nosdk_string_buffer_free(sb);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "insert failed: %s\n", PQerrorMessage(conn));
    }

    PQclear(res);

    char *response = "HTTP/1.1 200 OK";
    write(req->client_fd, response, strlen(response));
}

ssize_t writestr(int client_fd, char *s) {
    return write(client_fd, s, strlen(s));
}

void nosdk_pg_handle_get(struct nosdk_http_request *req, PGconn *conn) {
    char *table_name = get_table_name(req);
    struct nosdk_string_buffer *sb = nosdk_string_buffer_new();

    char query[256];
    snprintf(query, sizeof(query), "SELECT data FROM %s", table_name);

    PGresult *res = PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "select failed: %s", PQerrorMessage(conn));
        return;
    }

    nosdk_string_buffer_append(sb, "[");
    for (int i = 0; i < PQntuples(res); i++) {
        char *data = PQgetvalue(res, i, 0);
        nosdk_string_buffer_append(sb, data);
        if (i < PQntuples(res) - 1) {
            nosdk_string_buffer_append(sb, ",");
        }
    }
    nosdk_string_buffer_append(sb, "]");

    PQclear(res);

    char head_buf[256];

    writestr(req->client_fd, "HTTP/1.1 200 OK\r\n");
    writestr(req->client_fd, "Content-Type: application/json\r\n");
    snprintf(head_buf, sizeof(head_buf), "Content-Length: %d", sb->size);
    writestr(req->client_fd, head_buf);
    writestr(req->client_fd, "\r\n\r\n");
    write(req->client_fd, sb->data, sb->size);

    nosdk_string_buffer_free(sb);
}

void nosdk_pg_handler(struct nosdk_http_request *req) {
    printf("postgres handler %d %s\n", req->method, req->path);

    PGconn *conn = nosdk_pg_connect();
    if (conn == NULL) {
        char *response = "HTTP/1.1 500 Internal Error";
        write(req->client_fd, response, strlen(response));
        return;
    }

    if (req->method == HTTP_METHOD_POST) {
        nosdk_pg_handle_post(req, conn);
    } else if (req->method == HTTP_METHOD_GET) {
        nosdk_pg_handle_get(req, conn);
    } else {
        char *response = "HTTP/1.1 404 Not Found";
        write(req->client_fd, response, strlen(response));
    }

    nosdk_pg_disconnect(conn);
}
