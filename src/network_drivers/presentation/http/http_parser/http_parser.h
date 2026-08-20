// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_HTTP_PARSER_H
#define PROTOCORE_HTTP_PARSER_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP_PARSER

PROTOCORE_BEGIN_DECLS

// PROTOCORE_HTTP_PARSER_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

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
/**
 * @brief The per-slot request table every HTTP layer parses into and reads back out of.
 *
 * Not an entry: it is the shared cross-TU substrate a caller indexes by slot, not something the
 * namespace hands out, so it is reached by name.
 */
extern HttpReq http_pool[CONN_POOL_SLOTS];
typedef proto_bool (*HttpStreamBeginCb)(HttpReq *req);
typedef void (*HttpStreamDataCb)(HttpReq *req, const uint8_t *data, size_t len);
typedef void (*HttpStreamAbortCb)(HttpReq *req);
/** @brief What set_stream_hooks takes: begin, data, abort. */
typedef struct
{
    HttpStreamBeginCb begin;
    HttpStreamDataCb data;
    HttpStreamAbortCb abort;
} HttpParserSetStreamHooksArgs;
/** @brief What reset takes: req. */
typedef struct
{
    HttpReq *req;
} HttpParserResetArgs;
/** @brief What feed takes: req, byte. */
typedef struct
{
    HttpReq *req;
    uint8_t byte;
} HttpParserFeedArgs;
/** @brief What get_header takes: req, key. */
typedef struct
{
    const HttpReq *req;
    const char *key;
} HttpParserGetHeaderArgs;
/** @brief What get_cookie takes: req, name, out, out_size. */
typedef struct
{
    const HttpReq *req;
    const char *name;
    char *out;
    size_t out_size;
} HttpParserGetCookieArgs;
/** @brief What forwarded_client takes: req, ip_out, ip_cap, is_https. */
typedef struct
{
    const HttpReq *req;
    char *ip_out;
    size_t ip_cap;
    proto_bool *is_https;
} HttpParserForwardedClientArgs;
/** @brief What get_query takes: req, key. */
typedef struct
{
    const HttpReq *req;
    const char *key;
} HttpParserGetQueryArgs;
/** @brief What get_form takes: req, key, out, out_size. */
typedef struct
{
    const HttpReq *req;
    const char *key;
    char *out;
    size_t out_size;
} HttpParserGetFormArgs;
/** @brief What get_param takes: req, key. */
typedef struct
{
    const HttpReq *req;
    const char *key;
} HttpParserGetParamArgs;
/**
 * @brief HttpParser.
 *
 * A caller sets the members a call takes, invokes it through ::HttpParser with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   HttpParser.set_stream_hooks_args.begin = ...;
 *   HttpParser.set_stream_hooks_args.data = ...;
 *   HttpParser.set_stream_hooks_args.abort = ...;
 *   HttpParser.set_stream_hooks(work);
 *
 * @var HttpParserNs::set_stream_hooks_args  what set_stream_hooks takes: begin, data, abort
 * @var HttpParserNs::reset_args  what reset takes: req
 * @var HttpParserNs::feed_args  what feed takes: req, byte
 * @var HttpParserNs::get_header_args  what get_header takes: req, key
 * @var HttpParserNs::get_cookie_args  what get_cookie takes: req, name, out, out_size
 * @var HttpParserNs::forwarded_client_args  what forwarded_client takes: req, ip_out, ip_cap, is_https
 * @var HttpParserNs::get_query_args  what get_query takes: req, key
 * @var HttpParserNs::get_form_args  what get_form takes: req, key, out, out_size
 * @var HttpParserNs::get_param_args  what get_param takes: req, key
 * @var HttpParserNs::ok  a call's true/false outcome
 * @var HttpParserNs::text  the string a call reports
 * @var HttpParserNs::set_stream_hooks  set_stream_hooks
 * @var HttpParserNs::reset  reset
 * @var HttpParserNs::feed  feed
 * @var HttpParserNs::get_header  get_header
 * @var HttpParserNs::get_cookie  get_cookie
 * @var HttpParserNs::forwarded_client  forwarded_client
 * @var HttpParserNs::get_query  get_query
 * @var HttpParserNs::get_form  get_form
 * @var HttpParserNs::get_param  get_param
 *
 * @c work is PROTOCORE_HTTP_PARSER_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    HttpParserSetStreamHooksArgs set_stream_hooks_args;
    HttpParserResetArgs reset_args;
    HttpParserFeedArgs feed_args;
    HttpParserGetHeaderArgs get_header_args;
    HttpParserGetCookieArgs get_cookie_args;
    HttpParserForwardedClientArgs forwarded_client_args;
    HttpParserGetQueryArgs get_query_args;
    HttpParserGetFormArgs get_form_args;
    HttpParserGetParamArgs get_param_args;
    proto_bool ok;
    const char *text;
} HttpParserVars;

/** @brief The operands and the outcome. */
extern HttpParserVars HttpParserV;

/** @brief The entries. */
typedef struct
{
    void (*const set_stream_hooks)(uint8_t *restrict work);
    void (*const reset)(uint8_t *restrict work);
    void (*const feed)(uint8_t *restrict work);
    void (*const get_header)(uint8_t *restrict work);
    void (*const get_cookie)(uint8_t *restrict work);
    void (*const forwarded_client)(uint8_t *restrict work);
    void (*const get_query)(uint8_t *restrict work);
    void (*const get_form)(uint8_t *restrict work);
    void (*const get_param)(uint8_t *restrict work);
} HttpParserNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HttpParserV or a region of the borrow at a fixed offset.
void protocore_http_parser_set_stream_hooks(uint8_t *restrict work);
void protocore_http_parser_reset(uint8_t *restrict work);
void protocore_http_parser_feed(uint8_t *restrict work);
void protocore_http_parser_get_header(uint8_t *restrict work);
void protocore_http_parser_get_cookie(uint8_t *restrict work);
void protocore_http_parser_forwarded_client(uint8_t *restrict work);
void protocore_http_parser_get_query(uint8_t *restrict work);
void protocore_http_parser_get_form(uint8_t *restrict work);
void protocore_http_parser_get_param(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `HttpParser.set_stream_hooks(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HttpParserNs HttpParser __attribute__((unused)) = {
    .set_stream_hooks = protocore_http_parser_set_stream_hooks,
    .reset = protocore_http_parser_reset,
    .feed = protocore_http_parser_feed,
    .get_header = protocore_http_parser_get_header,
    .get_cookie = protocore_http_parser_get_cookie,
    .forwarded_client = protocore_http_parser_forwarded_client,
    .get_query = protocore_http_parser_get_query,
    .get_form = protocore_http_parser_get_form,
    .get_param = protocore_http_parser_get_param,
};

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

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_PARSER

#endif // PROTOCORE_HTTP_PARSER_H
