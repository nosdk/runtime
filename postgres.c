#include <libpq-fe.h>
#include <string.h>
#include <unistd.h>

#include "postgres.h"

void nosdk_pg_disconnect(PGconn *conn) { PQfinish(conn); }

PGconn *nosdk_pg_connect() {
    PGconn *conn =
        PQconnectdb("dbname=nosdk user=nosdk password=nosdk host=localhost");
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "connection error: %s\n", PQerrorMessage(conn));
        nosdk_pg_disconnect(conn);
        return NULL;
    }

    return conn;
}

void nosdk_pg_handler(struct nosdk_http_request *req) {
    printf("postgres handler %s\n", req->path);

    PGconn *conn = nosdk_pg_connect();
    if (conn == NULL) {
        char *response = "HTTP/1.1 500 Internal Error";
        write(req->client_fd, response, strlen(response));
        return;
    }

    char *response = "HTTP/1.1 200 OK";
    write(req->client_fd, response, strlen(response));

    nosdk_pg_disconnect(conn);
}
