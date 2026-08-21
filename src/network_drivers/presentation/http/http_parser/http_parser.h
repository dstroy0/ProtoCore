// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_HTTP_PARSER_H
#define PROTOCORE_HTTP_PARSER_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file http_parser.h
 * @brief HttpParser..
 *
 * @c work is PROTOCORE_HTTP_PARSER_BORROW bytes the CALLER took, at an address it knows. It is not held past the call,
 * so nothing here aliases it. How those bytes are carved is this module's and is never named here.
 */

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

typedef proto_bool (*HttpStreamBeginCb)(HttpReq *req);

typedef void (*HttpStreamDataCb)(HttpReq *req, const uint8_t *data, size_t len);

typedef void (*HttpStreamAbortCb)(HttpReq *req);

/**
 * @brief The per-slot request table every HTTP layer parses into and reads back out of.
 *
 * Not an entry: it is the shared cross-TU substrate a caller indexes by slot, not something the
 * namespace hands out, so it is reached by name.
 */
extern HttpReq http_pool[CONN_POOL_SLOTS];

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*set_stream_hooks)(uint8_t *, HttpStreamBeginCb, HttpStreamDataCb, HttpStreamAbortCb);
    void (*reset)(uint8_t *, HttpReq *);
    void (*feed)(uint8_t *, HttpReq *, uint8_t);
    const char *(*get_header)(uint8_t *, const HttpReq *, const char *);
    proto_bool (*get_cookie)(uint8_t *, const HttpReq *, const char *, char *, size_t);
    proto_bool (*forwarded_client)(uint8_t *, const HttpReq *, char *, size_t, proto_bool *);
    const char *(*get_query)(uint8_t *, const HttpReq *, const char *);
    proto_bool (*get_form)(uint8_t *, const HttpReq *, const char *, char *, size_t);
    const char *(*get_param)(uint8_t *, const HttpReq *, const char *);
} HttpParserNs;
PROTOCORE_NS_LAYOUT(HttpParserNs, set_stream_hooks, reset, feed, get_header, get_cookie, forwarded_client, get_query,
                    get_form, get_param);

/**
 * @brief Set_stream_hooks.
 * @param work PROTOCORE_HTTP_PARSER_BORROW bytes the caller took. Not held past the call.
 * @param begin Begin
 * @param data Data
 * @param abort Abort
 */
void protocore_http_parser_set_stream_hooks(uint8_t *work, HttpStreamBeginCb begin, HttpStreamDataCb data,
                                            HttpStreamAbortCb abort);
/**
 * @brief Reset.
 * @param work PROTOCORE_HTTP_PARSER_BORROW bytes the caller took. Not held past the call.
 * @param req Req
 */
void protocore_http_parser_reset(uint8_t *work, HttpReq *req);
/**
 * @brief Feed.
 * @param work PROTOCORE_HTTP_PARSER_BORROW bytes the caller took. Not held past the call.
 * @param req Req
 * @param byte Byte
 */
void protocore_http_parser_feed(uint8_t *work, HttpReq *req, uint8_t byte);
/**
 * @brief Get_header.
 * @param work PROTOCORE_HTTP_PARSER_BORROW bytes the caller took. Not held past the call.
 * @param req Req
 * @param key Key
 * @return The const char *.
 */
const char *protocore_http_parser_get_header(uint8_t *work, const HttpReq *req, const char *key);
/**
 * @brief Get_cookie.
 * @param work PROTOCORE_HTTP_PARSER_BORROW bytes the caller took. Not held past the call.
 * @param req Req
 * @param name Name
 * @param out Out
 * @param out_size Out size
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_http_parser_get_cookie(uint8_t *work, const HttpReq *req, const char *name, char *out,
                                            size_t out_size);
/**
 * @brief Forwarded_client.
 * @param work PROTOCORE_HTTP_PARSER_BORROW bytes the caller took. Not held past the call.
 * @param req Req
 * @param ip_out Ip out
 * @param ip_cap Ip cap
 * @param is_https Is https
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_http_parser_forwarded_client(uint8_t *work, const HttpReq *req, char *ip_out, size_t ip_cap,
                                                  proto_bool *is_https);
/**
 * @brief Get_query.
 * @param work PROTOCORE_HTTP_PARSER_BORROW bytes the caller took. Not held past the call.
 * @param req Req
 * @param key Key
 * @return The const char *.
 */
const char *protocore_http_parser_get_query(uint8_t *work, const HttpReq *req, const char *key);
/**
 * @brief Get_form.
 * @param work PROTOCORE_HTTP_PARSER_BORROW bytes the caller took. Not held past the call.
 * @param req Req
 * @param key Key
 * @param out Out
 * @param out_size Out size
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_http_parser_get_form(uint8_t *work, const HttpReq *req, const char *key, char *out,
                                          size_t out_size);
/**
 * @brief Get_param.
 * @param work PROTOCORE_HTTP_PARSER_BORROW bytes the caller took. Not held past the call.
 * @param req Req
 * @param key Key
 * @return The const char *.
 */
const char *protocore_http_parser_get_param(uint8_t *work, const HttpReq *req, const char *key);

typedef proto_bool (*HttpStreamBeginCb)(HttpReq *req);
typedef void (*HttpStreamDataCb)(HttpReq *req, const uint8_t *data, size_t len);
/**
 * @brief The PROTOCORE_HTTP_PARSER_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_http_parser_span(void);

/** @brief Module namespace. */
PROTOCORE_NS HttpParserNs HttpParser PROTOCORE_UNUSED = {.set_stream_hooks = protocore_http_parser_set_stream_hooks,
                                                         .reset = protocore_http_parser_reset,
                                                         .feed = protocore_http_parser_feed,
                                                         .get_header = protocore_http_parser_get_header,
                                                         .get_cookie = protocore_http_parser_get_cookie,
                                                         .forwarded_client = protocore_http_parser_forwarded_client,
                                                         .get_query = protocore_http_parser_get_query,
                                                         .get_form = protocore_http_parser_get_form,
                                                         .get_param = protocore_http_parser_get_param};

PROTOCORE_END_DECLS

#endif // PROTOCORE_HTTP_PARSER_H
