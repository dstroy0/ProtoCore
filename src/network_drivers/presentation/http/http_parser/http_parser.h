// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_HTTP_PARSER_H
#define PROTOCORE_HTTP_PARSER_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

typedef enum PROTO_ENUM_PACKED
{
    PARSE_METHOD,
    PARSE_PATH,
    PARSE_QUERY,
    PARSE_VERSION,
    PARSE_HEADER_KEY,
    PARSE_HEADER_VAL,
    PARSE_EXPECT_LF,
    PARSE_EXPECT_BODY_LF,
    PARSE_BODY,
    PARSE_COMPLETE,
    PARSE_ERROR,
    PARSE_ENTITY_TOO_LARGE,
    PARSE_URI_TOO_LONG
} ParseState;

typedef enum PROTO_ENUM_PACKED
{
    HTTP_UNKNOWN = 0,
    HTTP_10,
    HTTP_11
} HttpVersion;

typedef struct
{
    char key[MAX_KEY_LEN];
    char val[MAX_VAL_LEN];
} Header;

typedef struct
{
    char key[QUERY_KEY_LEN];
    char val[QUERY_VAL_LEN];
} QueryParam;

typedef struct HttpReq
{
    uint8_t slot_id;
    ParseState parse_state;
    HttpVersion version;
    uint32_t _version_hash;

    char method[PROTOCORE_METHOD_BUF_SIZE];
    char path[MAX_PATH_LEN];
    size_t path_idx;

    char query[MAX_QUERY_LEN];
    size_t query_idx;
    QueryParam query_params[MAX_QUERY_PARAMS];
    uint8_t query_count;

    QueryParam path_params[MAX_PATH_PARAMS];
    uint8_t path_param_count;

#if PROTOCORE_CAPTURE_AUTH_HEADER
    char authorization[PROTOCORE_AUTH_HDR_CAP];
    uint16_t auth_idx;
    proto_bool cur_is_auth;
#endif

    Header headers[MAX_HEADERS];
    uint8_t header_count;
    size_t current_token_idx;

    char cur_key[MAX_KEY_LEN];
    char cur_val[MAX_VAL_LEN];

    size_t content_length;
    uint8_t content_length_count;
    uint8_t host_count;
    size_t body_bytes_read;

    uint8_t body[BODY_BUF_SIZE + 1];
    size_t body_len;

#if PROTOCORE_ENABLE_STREAM_BODY
    proto_bool body_streaming;
#endif
} HttpReq;

extern HttpReq http_pool[CONN_POOL_SLOTS];

#if PROTOCORE_ENABLE_STREAM_BODY

typedef proto_bool (*HttpStreamBeginCb)(HttpReq *req);

typedef void (*HttpStreamDataCb)(HttpReq *req, const uint8_t *data, size_t len);

typedef void (*HttpStreamAbortCb)(HttpReq *req);

void http_parser_set_stream_hooks(HttpStreamBeginCb begin, HttpStreamDataCb data, HttpStreamAbortCb abort);
#endif

void http_parser_reset(HttpReq *req);

void http_parser_feed(HttpReq *req, uint8_t byte);

const char *http_get_header(const HttpReq *req, const char *key);

proto_bool http_get_cookie(const HttpReq *req, const char *name, char *out, size_t out_size);

proto_bool http_forwarded_client(const HttpReq *req, char *ip_out, size_t ip_cap, proto_bool *is_https);

const char *http_get_query(const HttpReq *req, const char *key);

proto_bool http_get_form(const HttpReq *req, const char *key, char *out, size_t out_size);

const char *http_get_param(const HttpReq *req, const char *key);

PROTOCORE_END_DECLS

#endif
