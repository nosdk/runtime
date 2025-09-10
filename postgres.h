#ifndef _NOSDK_POSTGRES_H
#define _NOSDK_POSTGRES_H

#include <libpq-fe.h>

#include "http.h"

#define PG_POOL_MAX 10

struct nosdk_pg {
    PGconn *pool[PG_POOL_MAX];
    int in_use[PG_POOL_MAX];
};

void nosdk_pg_handler(struct nosdk_http_request *req);

#endif // _NOSDK_POSTGRES_H
