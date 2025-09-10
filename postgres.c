#include <libpq-fe.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "http.h"
#include "postgres.h"
#include "util.h"

struct nosdk_pg pg_pool = {0};

int nosdk_pg_init() {
    if (pg_pool.initialized) {
        return 0;
    }

    int result = pthread_mutex_init(&pg_pool.mutex, NULL);
    if (result != 0) {
        fprintf(stderr, "failed to initialize pool mutex: %d\n", result);
        return -1;
    }

    for (int i = 0; i < PG_POOL_MAX; i++) {
        pg_pool.pool[i] = NULL;
        pg_pool.in_use[i] = false;
    }

    pg_pool.initialized = true;
    return 0;
}

void nosdk_pg_disconnect(PGconn *conn) { PQfinish(conn); }

PGconn *nosdk_pg_get_connection() {
    pthread_mutex_lock(&pg_pool.mutex);

    for (int i = 0; i < PG_POOL_MAX; i++) {
        if (!pg_pool.in_use[i]) {
            if (pg_pool.pool[i] != NULL) {
                if (PQstatus(pg_pool.pool[i]) != CONNECTION_OK) {
                    nosdk_debugf("cleaning up stale connection\n");
                    PQfinish(pg_pool.pool[i]);
                    pg_pool.pool[i] = NULL;
                } else {
                    pg_pool.in_use[i] = 1;
                    pthread_mutex_unlock(&pg_pool.mutex);
                    return pg_pool.pool[i];
                }
            } else {
                PGconn *conn = PQconnectdb(
                    "dbname=nosdk user=nosdk password=nosdk host=localhost "
                    "port=15432");
                if (PQstatus(conn) != CONNECTION_OK) {
                    fprintf(
                        stderr, "connection error: %s\n", PQerrorMessage(conn));
                    nosdk_pg_disconnect(conn);
                    pthread_mutex_unlock(&pg_pool.mutex);
                    return NULL;
                }
                pg_pool.in_use[i] = 1;
                pg_pool.pool[i] = conn;
                pthread_mutex_unlock(&pg_pool.mutex);
                return conn;
            }
        }
    }
    pthread_mutex_unlock(&pg_pool.mutex);
    fprintf(stderr, "all postgres connections in use\n");
    return NULL;
}

void nosdk_pg_connection_release(PGconn *conn) {
    pthread_mutex_lock(&pg_pool.mutex);
    for (int i = 0; i < PG_POOL_MAX; i++) {
        if (pg_pool.pool[i] == conn) {
            pg_pool.in_use[i] = 0;
        }
    }
    pthread_mutex_unlock(&pg_pool.mutex);
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
    char *table_prefix = "/db/tables/";
    char *name = strdup(&req->path[strlen(table_prefix)]);
    int len = strlen(name);

    for (int i = 0; i < len; i++) {
        if (name[i] == '?') {
            name[i] = '\0';
        }
    }
    return name;
}

int nosdk_pg_insert_item(PGconn *conn, char *table_name, char *item) {
    const char *paramValues[1] = {item};
    char query[128];

    snprintf(
        query, sizeof(query), "INSERT INTO %s (data) VALUES ($1::jsonb)",
        table_name);

    PGresult *res =
        PQexecParams(conn, query, 1, NULL, paramValues, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "insert failed: %s\n", PQerrorMessage(conn));
        return -1;
    }

    PQclear(res);
    return 0;
}

struct json_array_iter {
    char *data;
    int data_len;
    int pos;
};

int json_array_next_item(
    struct json_array_iter *iter, int *start_pos, int *len) {
    int start = 0;
    int end = 0;
    int depth = 0;

    while (end == 0 && iter->pos < iter->data_len) {
        char this_char = iter->data[iter->pos];

        if (this_char == '{') {
            start = iter->pos;
            depth++;
        } else if (this_char == '}') {
            depth--;
        }

        iter->pos++;

        if (start != 0 && depth == 0) {
            end = iter->pos;
            break;
        }
    }

    *start_pos = start;
    *len = (end - start);

    return start;
}

int is_operator(char c) {
    if (c == '=' || c == '<' || c == '>') {
        return 1;
    }
    return 0;
}

char *val2pgtype(char *val) {
    int seen_dot = 0;

    for (int i = 0; i < strlen(val); i++) {
        if (val[i] == '.') {
            seen_dot = 1;
        } else if (val[i] < 48 || val[i] > 57) {
            return "text";
        }
    }

    if (seen_dot) {
        return "real";
    }

    return "integer";
}

int translate_query_string(
    struct nosdk_string_buffer *sb, const char *paramValues[16], char *query) {

    urldecode2(query, query);

    int len = strlen(query);
    int in_key = 1;
    int key_len = 0;
    int val_len = 0;

    char key[64];
    char val[64];
    char operator;

    int n_clauses = 0;

    for (int i = 1; i < len; i++) {
        char this_char = query[i];

        if (in_key && !is_operator(this_char)) {
            key[key_len] = this_char;
            key_len++;
        } else if (in_key && is_operator(this_char)) {
            operator = this_char;
            in_key = 0;
        } else if (!in_key && this_char != '&') {
            val[val_len] = this_char;
            val_len++;
        }

        if (this_char == '&' || i == (len - 1)) {
            char *word = "WHERE";
            if (n_clauses > 0) {
                word = "AND";
            }

            val[val_len] = '\0';
            paramValues[n_clauses] = strdup(val);
            n_clauses++;
            nosdk_string_buffer_append(
                sb, " %s (data->>'%.*s')::%s %c $%d", word, key_len, key,
                val2pgtype(val), operator, n_clauses);

            key_len = 0;
            val_len = 0;
            in_key = 1;
        }
    }

    return n_clauses;
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

    if (data[0] == '{') {
        if (nosdk_pg_insert_item(conn, table_name, data) != 0) {
            nosdk_http_respond(
                req, HTTP_STATUS_INVALID_REQUEST, "text/plain", NULL, 0);
            return;
        }
    } else if (data[0] == '[') {
        struct json_array_iter iter = {
            .data = data,
            .data_len = strlen(data),
        };
        int start_pos = 0;
        int len = 0;
        while (json_array_next_item(&iter, &start_pos, &len) > 0) {
            // add null terminator. there should always be a , or ] after an
            // array item so...
            char c = data[start_pos + len];
            data[start_pos + len] = '\0';
            if (nosdk_pg_insert_item(conn, table_name, &data[start_pos]) != 0) {
                nosdk_http_respond(
                    req, HTTP_STATUS_INVALID_REQUEST, "text/plain", NULL, 0);
                return;
            }
            data[start_pos + len] = c;
        }
    }

    nosdk_http_respond(req, HTTP_STATUS_OK, "text/plain", NULL, 0);
}

ssize_t writestr(int client_fd, char *s) {
    return write(client_fd, s, strlen(s));
}

void nosdk_pg_handle_get(struct nosdk_http_request *req, PGconn *conn) {
    char *table_name = get_table_name(req);
    const char *paramValues[16] = {0};
    int n_params = 0;

    struct nosdk_string_buffer *qbuf = nosdk_string_buffer_new();
    struct nosdk_string_buffer *sb = nosdk_string_buffer_new();

    nosdk_string_buffer_append(qbuf, "SELECT data FROM %s", table_name);

    char *qstr = strstr(req->path, "?");
    if (qstr != NULL) {
        n_params = translate_query_string(qbuf, paramValues, qstr);
    }

    nosdk_debugf("query: %s\n", qbuf->data);

    PGresult *res = PQexecParams(
        conn, qbuf->data, n_params, NULL, paramValues, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "select failed: %s", PQerrorMessage(conn));
        nosdk_http_respond(
            req, HTTP_STATUS_INVALID_REQUEST, "text/plain", NULL, 0);
        nosdk_string_buffer_free(sb);
        nosdk_string_buffer_free(qbuf);
        PQclear(res);
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

    nosdk_http_respond(
        req, HTTP_STATUS_OK, "application/json", sb->data, sb->size);
    nosdk_string_buffer_free(sb);
    nosdk_string_buffer_free(qbuf);
}

void nosdk_pg_handler(struct nosdk_http_request *req) {
    nosdk_pg_init();

    PGconn *conn = nosdk_pg_get_connection();
    if (conn == NULL) {
        nosdk_http_respond(
            req, HTTP_STATUS_INTERNAL_ERROR, "text/plain", NULL, 0);
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

    nosdk_pg_connection_release(conn);
}
