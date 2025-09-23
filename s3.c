#include "s3.h"
#include "http.h"
#include <stdlib.h>

struct nosdk_s3_ctx *s3_ctx;

void s3_init() {
    if (s3_ctx != NULL) {
        return;
    }
    s3_ctx = malloc(sizeof(struct nosdk_s3_ctx));
    AWS_ZERO_STRUCT(s3_ctx->client_config);

    s3_ctx->allocator = aws_default_allocator();

    aws_common_library_init(s3_ctx->allocator);
    aws_io_library_init(s3_ctx->allocator);
    aws_http_library_init(s3_ctx->allocator);
    aws_auth_library_init(s3_ctx->allocator);
    aws_s3_library_init(s3_ctx->allocator);

    // set up logging
    s3_ctx->ll = AWS_LL_ERROR;
    struct aws_logger_standard_options log_opts = {
        .level = s3_ctx->ll,
        .file = stderr,
    };
    aws_logger_init_standard(&s3_ctx->logger, s3_ctx->allocator, &log_opts);
    aws_logger_set(&s3_ctx->logger);

    // event loop
    struct aws_event_loop_group *event_loop_group =
        aws_event_loop_group_new_default(s3_ctx->allocator, 0, NULL);

    // resolver
    struct aws_host_resolver_default_options resolver_options = {
        .el_group = event_loop_group,
        .max_entries = 8,
    };
    struct aws_host_resolver *resolver =
        aws_host_resolver_new_default(s3_ctx->allocator, &resolver_options);

    // client bootstrap
    struct aws_client_bootstrap_options bootstrap_options = {
        .event_loop_group = event_loop_group,
        .host_resolver = resolver,
    };

    s3_ctx->client_config.client_bootstrap =
        aws_client_bootstrap_new(s3_ctx->allocator, &bootstrap_options);
    s3_ctx->client_config.region = aws_byte_cursor_from_c_str("us-east-1");

    // credentials

    struct aws_credentials_provider_static_options static_options = {
        .access_key_id = aws_byte_cursor_from_c_str("minioadmin"),
        .secret_access_key = aws_byte_cursor_from_c_str("minioadmin"),
    };

    s3_ctx->credentials_provider =
        aws_credentials_provider_new_static(s3_ctx->allocator, &static_options);

    struct aws_credentials_provider_chain_default_options
        credentials_provider_options;
    AWS_ZERO_STRUCT(credentials_provider_options);
    credentials_provider_options.bootstrap =
        s3_ctx->client_config.client_bootstrap;

    // signing config
    // s3_ctx->credentials_provider =
    // aws_credentials_provider_new_chain_default(
    //     s3_ctx->allocator, &credentials_provider_options);
    aws_s3_init_default_signing_config(
        &s3_ctx->signing_config, s3_ctx->client_config.region,
        s3_ctx->credentials_provider);

    s3_ctx->client_config.signing_config = &s3_ctx->signing_config;
    s3_ctx->client_config.throughput_target_gbps = 1.0;
    s3_ctx->client_config.retry_strategy = NULL;
    s3_ctx->client_config.tls_mode = AWS_MR_TLS_DISABLED;

    s3_ctx->client =
        aws_s3_client_new(s3_ctx->allocator, &s3_ctx->client_config);
}

void s3_deinit() { aws_s3_library_clean_up(); }

struct nosdk_s3_request_ctx *
nosdk_s3_request_ctx_new(struct nosdk_http_request *req) {
    struct nosdk_s3_request_ctx *ctx =
        malloc(sizeof(struct nosdk_s3_request_ctx));

    aws_mutex_init(&ctx->mutex);
    ctx->c_var = (struct aws_condition_variable)AWS_CONDITION_VARIABLE_INIT;
    ctx->req = req;

    return ctx;
}

void nosdk_s3_request_ctx_free(struct nosdk_s3_request_ctx *ctx) {
    aws_mutex_clean_up(&ctx->mutex);
    aws_condition_variable_clean_up(&ctx->c_var);
    free(ctx);
}

void s3_create_bucket_finish_cb(
    struct aws_s3_meta_request *meta_request,
    const struct aws_s3_meta_request_result *meta_request_result,
    void *user_data) {

    struct nosdk_s3_request_ctx *ctx = (struct nosdk_s3_request_ctx *)user_data;

    aws_mutex_lock(&ctx->mutex);
    aws_condition_variable_notify_one(&ctx->c_var);
    aws_mutex_unlock(&ctx->mutex);

    ctx->result_code = meta_request_result->error_code;
    ctx->response_status = meta_request_result->response_status;
}

void nosdk_s3_host_header(struct aws_http_message *message) {
    struct aws_http_header host_header = {
        .name = aws_byte_cursor_from_c_str("Host"),
        .value = aws_byte_cursor_from_c_str("localhost:9000"),
    };
    aws_http_message_add_header(message, host_header);
}

int nosdk_s3_set_endpoint(struct aws_s3_meta_request_options *options) {
    struct aws_uri *endpoint_uri =
        aws_mem_calloc(s3_ctx->allocator, 1, sizeof(struct aws_uri));
    struct aws_byte_cursor endpoint_cursor =
        aws_byte_cursor_from_c_str("http://localhost:9000");

    if (aws_uri_init_parse(endpoint_uri, s3_ctx->allocator, &endpoint_cursor) !=
        AWS_OP_SUCCESS) {
        printf("Failed to parse endpoint URI\n");
        aws_mem_release(s3_ctx->allocator, endpoint_uri);
        return -1;
    }

    options->endpoint = endpoint_uri;
    return 0;
}

int s3_create_bucket(struct nosdk_s3_request_ctx *ctx, char *bucket_name) {
    struct aws_http_message *message =
        aws_http_message_new_request(s3_ctx->allocator);
    if (!message) {
        return -1;
    }

    aws_http_message_set_request_method(
        message, aws_byte_cursor_from_c_str("PUT"));

    char path[256];
    snprintf(path, sizeof(path), "/%s", bucket_name);
    aws_http_message_set_request_path(
        message, aws_byte_cursor_from_c_str(path));

    nosdk_s3_host_header(message);

    struct aws_s3_meta_request_options options = {
        .type = AWS_S3_META_REQUEST_TYPE_DEFAULT,
        .operation_name = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("CreateBucket"),
        .finish_callback = s3_create_bucket_finish_cb,
        .user_data = ctx,
        .message = message,
    };

    nosdk_s3_set_endpoint(&options);

    struct aws_s3_meta_request *req =
        aws_s3_client_make_meta_request(s3_ctx->client, &options);

    aws_http_message_release(message);

    if (req == NULL) {
        return -1;
    }

    aws_mutex_lock(&ctx->mutex);
    aws_condition_variable_wait(&ctx->c_var, &ctx->mutex);
    aws_mutex_unlock(&ctx->mutex);

    return ctx->result_code == AWS_ERROR_SUCCESS ? 0 : 1;
}

char *request_obj_path(struct nosdk_http_request *req) { return &req->path[5]; }

char *get_bucket_name(struct nosdk_http_request *req) {
    char *blob_prefix = "/blob/";
    char *name = strdup(&req->path[strlen(blob_prefix)]);
    int len = strlen(name);

    for (int i = 0; i < len; i++) {
        name[i] = tolower(name[i]);
        if (name[i] == '?' || name[i] == '/') {
            name[i] = '\0';
        }
    }
    return name;
}

static void s3_put_object_finish_cb(
    struct aws_s3_meta_request *meta_request,
    const struct aws_s3_meta_request_result *result,
    void *user_data) {

    struct nosdk_s3_request_ctx *ctx = (struct nosdk_s3_request_ctx *)user_data;

    aws_mutex_lock(&ctx->mutex);
    ctx->result_code = result->error_code;
    ctx->response_status = result->response_status;

    if (result->error_code != AWS_ERROR_SUCCESS) {
        printf(
            "PutObject failed with error: %s\n",
            aws_error_name(result->error_code));
    } else {
        printf(
            "PutObject completed successfully, status: %d\n",
            result->response_status);
    }

    aws_condition_variable_notify_one(&ctx->c_var);
    aws_mutex_unlock(&ctx->mutex);
}

int s3_put_object(struct nosdk_s3_request_ctx *ctx) {

    struct aws_http_message *message =
        aws_http_message_new_request(s3_ctx->allocator);
    if (!message) {
        return -1;
    }

    aws_http_message_set_request_method(
        message, aws_byte_cursor_from_c_str("PUT"));

    char *obj_path = request_obj_path(ctx->req);
    printf("object path %s\n", obj_path);

    aws_http_message_set_request_path(
        message, aws_byte_cursor_from_c_str(obj_path));

    nosdk_s3_host_header(message);

    char content_length[32];
    snprintf(
        content_length, sizeof(content_length), "%d", ctx->req->content_length);
    struct aws_http_header clen = {
        .name = aws_byte_cursor_from_c_str("Content-Length"),
        .value = aws_byte_cursor_from_c_str(content_length),
    };
    aws_http_message_add_header(message, clen);

    // TODO: actually stream body data
    char *body_data = nosdk_http_request_body_alloc(ctx->req);
    struct aws_byte_cursor data_cursor =
        aws_byte_cursor_from_array(body_data, ctx->req->content_length);
    struct aws_input_stream *input_stream =
        aws_input_stream_new_from_cursor(s3_ctx->allocator, &data_cursor);
    if (!input_stream) {
        printf("Failed to create input stream\n");
        aws_http_message_release(message);
        return -1;
    }

    aws_http_message_set_body_stream(message, input_stream);

    struct aws_s3_meta_request_options options = {
        .type = AWS_S3_META_REQUEST_TYPE_DEFAULT,
        .operation_name = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("PutObject"),
        .finish_callback = s3_put_object_finish_cb,
        .user_data = ctx,
        .message = message,
    };

    if (nosdk_s3_set_endpoint(&options) != 0) {
        free(body_data);
        aws_input_stream_release(input_stream);
        aws_http_message_release(message);
        return -1;
    }

    struct aws_s3_meta_request *meta_request =
        aws_s3_client_make_meta_request(s3_ctx->client, &options);

    aws_input_stream_release(input_stream);
    aws_http_message_release(message);

    if (meta_request == NULL) {
        printf("failed to make meta request\n");
        return -1;
    }

    aws_mutex_lock(&ctx->mutex);
    aws_condition_variable_wait(&ctx->c_var, &ctx->mutex);
    aws_mutex_unlock(&ctx->mutex);

    int result = ctx->result_code;

    return result == AWS_ERROR_SUCCESS ? 0 : -1;
}

void nosdk_s3_handler(struct nosdk_http_request *req) {
    printf("blob request %s\n", req->path);
    struct nosdk_s3_request_ctx *ctx = nosdk_s3_request_ctx_new(req);
    s3_init();

    if (req->method == HTTP_METHOD_PUT) {
        if (s3_put_object(ctx) == 0) {
            nosdk_http_respond(req, HTTP_STATUS_OK, "text/plain", NULL, 0);
        } else {
            if (ctx->response_status == 404) {
                // attempt to create bucket
                char *bucket_name = get_bucket_name(req);
                if (s3_create_bucket(ctx, bucket_name) == 0) {
                    nosdk_s3_handler(req);
                    return;
                }
            }
            nosdk_http_respond(
                req, HTTP_STATUS_INTERNAL_ERROR, "text/plain", NULL, 0);
        }
    } else {
        nosdk_http_respond(req, HTTP_STATUS_NOT_FOUND, "text/plain", NULL, 0);
    }
}
