#ifndef _NOSDK_S3
#define _NOSDK_S3

#include <aws/auth/credentials.h>
#include <aws/common/condition_variable.h>
#include <aws/common/mutex.h>
#include <aws/common/uri.h>
#include <aws/http/http.h>
#include <aws/http/request_response.h>
#include <aws/io/channel_bootstrap.h>
#include <aws/io/event_loop.h>
#include <aws/io/logging.h>
#include <aws/io/stream.h>
#include <aws/s3/s3.h>
#include <aws/s3/s3_client.h>

#include "http.h"

struct nosdk_s3_ctx {
    struct aws_allocator *allocator;
    struct aws_s3_client_config client_config;
    struct aws_signing_config_aws signing_config;
    struct aws_credentials_provider *credentials_provider;
    struct aws_s3_client *client;
    struct aws_logger logger;
    enum aws_log_level ll;
};

struct nosdk_s3_request_ctx {
    struct nosdk_http_request *req;
    struct aws_mutex mutex;
    struct aws_condition_variable c_var;
    int result_code;
    int response_status;
};

void nosdk_s3_handler(struct nosdk_http_request *req);

#endif // _NOSDK_S3
