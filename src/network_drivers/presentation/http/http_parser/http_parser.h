// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_parser.h
 * @brief Standalone HTTP/1.1 request parser - no transport dependency.
 *
 * The parser is a pure byte-stream state machine.  It has no knowledge of
 * ring buffers, TCP PCBs, or FreeRTOS.  Feed it bytes one at a time via
 * `http_parser_feed()` and inspect `HttpReq::parse_state` to know when the
 * request is ready.
 *
 * **State machine**
 * ```
 * PARSE_METHOD       ──space──────► PARSE_PATH
 * PARSE_PATH         ──space──────► PARSE_VERSION
 * PARSE_PATH         ──'?'────────► PARSE_QUERY
 * PARSE_QUERY        ──space──────► PARSE_VERSION  (calls parse_query_params)
 * PARSE_VERSION      ──CR─────────► PARSE_EXPECT_LF
 * PARSE_EXPECT_LF    ──LF─────────► PARSE_HEADER_KEY
 * PARSE_HEADER_KEY   ──':'────────► PARSE_HEADER_VAL
 * PARSE_HEADER_KEY   ──CR─────────► PARSE_EXPECT_BODY_LF  (blank line)
 * PARSE_HEADER_VAL   ──CR─────────► PARSE_EXPECT_LF  (stores header)
 * PARSE_EXPECT_BODY_LF ──LF (CL=0)──► PARSE_COMPLETE
 * PARSE_EXPECT_BODY_LF ──LF (CL>BUF)► PARSE_ENTITY_TOO_LARGE  (→ 413)
 * PARSE_EXPECT_BODY_LF ──LF (else)──► PARSE_BODY
 * PARSE_BODY         ──(all read)──► PARSE_COMPLETE
 * PARSE_PATH (overflow) ───────────► PARSE_URI_TOO_LONG       (→ 414)
 * Any state + protocol error ──────► PARSE_ERROR             (→ 400)
 * ```
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HTTP_PARSER_H
#define PROTOCORE_HTTP_PARSER_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Parser state enumeration
// ---------------------------------------------------------------------------

/**
 * @brief States of the HTTP/1.1 request parser.
 *
 * Advance via http_parser_feed().  The application layer inspects this
 * after each feed call or after draining a complete chunk.
 */
typedef enum PROTO_ENUM_PACKED
{
    PARSE_METHOD,           ///< Reading the HTTP method (GET, POST, …).
    PARSE_PATH,             ///< Reading the URL path component.
    PARSE_QUERY,            ///< Reading the raw query string (after `?`).
    PARSE_VERSION,          ///< Accumulating `HTTP/1.x` - hashed for validation.
    PARSE_HEADER_KEY,       ///< Reading a header field name.
    PARSE_HEADER_VAL,       ///< Reading a header field value.
    PARSE_EXPECT_LF,        ///< Consuming the LF of a header-line CRLF pair.
    PARSE_EXPECT_BODY_LF,   ///< Consuming the LF of the blank-line CRLF.
    PARSE_BODY,             ///< Reading the request body.
    PARSE_COMPLETE,         ///< Full request parsed; ready for dispatch.
    PARSE_ERROR,            ///< Unrecoverable parse failure → 400.
    PARSE_ENTITY_TOO_LARGE, ///< Content-Length > BODY_BUF_SIZE → 413.
    PARSE_URI_TOO_LONG      ///< Path exceeds MAX_PATH_LEN → 414.
} ParseState;

/**
 * @brief Parsed HTTP protocol version.
 *
 * Populated from the request line (`HTTP/1.0` or `HTTP/1.1`) using an FNV-1a
 * hash accumulated during `PARSE_VERSION`.  The application layer may use
 * this to drive keep-alive semantics: HTTP/1.1 defaults to persistent
 * connections; HTTP/1.0 defaults to close.
 */
typedef enum PROTO_ENUM_PACKED
{
    HTTP_UNKNOWN = 0, ///< Version string did not match any known token.
    HTTP_10,          ///< HTTP/1.0 - close semantics by default.
    HTTP_11           ///< HTTP/1.1 - persistent connection by default.
} HttpVersion;

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

/** @brief A single HTTP header field (key: value). */
typedef struct
{
    char key[MAX_KEY_LEN]; ///< Field name, null-terminated.
    char val[MAX_VAL_LEN]; ///< Field value, null-terminated.
} Header;

/** @brief A single parsed query-string parameter. */
typedef struct
{
    char key[QUERY_KEY_LEN]; ///< Parameter name, null-terminated.
    char val[QUERY_VAL_LEN]; ///< Parameter value (empty string if absent).
} QueryParam;

/**
 * @brief Fully-parsed HTTP/1.1 request.
 *
 * Populated incrementally by http_parser_feed().  Valid for dispatch
 * only when `parse_state == PARSE_COMPLETE`.
 *
 * Call http_parser_reset() to recycle this struct for the next request.
 */
typedef struct
{
    uint8_t slot_id;        ///< Transport slot index (set by presentation layer).
    ParseState parse_state; ///< Current parser state.
    HttpVersion version;    ///< Protocol version parsed from the request line.
    uint32_t _version_hash; ///< FNV-1a accumulator for version validation (internal).

    char method[PC_METHOD_BUF_SIZE]; ///< HTTP method, null-terminated (OPTIONS, or WebDAV methods when enabled).
    char path[MAX_PATH_LEN];         ///< URL path, null-terminated; no query string.
    size_t path_idx;                 ///< Write cursor into path[].

    char query[MAX_QUERY_LEN];                 ///< Raw query string (after `?`).
    size_t query_idx;                          ///< Write cursor into query[].
    QueryParam query_params[MAX_QUERY_PARAMS]; ///< Parsed key=value pairs.
    uint8_t query_count;                       ///< Valid entries in query_params[].

    QueryParam path_params[MAX_PATH_PARAMS]; ///< `:name` captures from the matched route.
    uint8_t path_param_count;                ///< Valid entries in path_params[].

#if PC_CAPTURE_AUTH_HEADER
    char authorization[PC_AUTH_HDR_CAP]; ///< Full Authorization header value (Digest/JWT exceed MAX_VAL_LEN).
    uint16_t auth_idx;                   ///< Write cursor into authorization[] (parser-internal).
    proto_bool cur_is_auth;              ///< True while parsing an Authorization header value (parser-internal).
#endif

    Header headers[MAX_HEADERS]; ///< Captured header fields.
    uint8_t header_count;        ///< Valid entries in headers[].
    size_t current_token_idx;    ///< Write cursor shared by key/value sub-states.

    // Scratch copies of the header currently being parsed, populated even for
    // headers beyond MAX_HEADERS so that Host / Content-Length detection and
    // counting are independent of the storage cap (RFC 7230 §5.4, §3.3.2).
    char cur_key[MAX_KEY_LEN]; ///< Field-name of the in-progress header.
    char cur_val[MAX_VAL_LEN]; ///< Field-value of the in-progress header.

    size_t content_length;        ///< Value of Content-Length header (0 if absent).
    uint8_t content_length_count; ///< Number of Content-Length fields seen (RFC 7230 §3.3.2).
    uint8_t host_count;           ///< Number of Host fields seen (RFC 7230 §5.4).
    size_t body_bytes_read;       ///< Body bytes received (may exceed BODY_BUF_SIZE).

    uint8_t body[BODY_BUF_SIZE + 1]; ///< Stored body bytes, always null-terminated.
    size_t body_len;                 ///< Bytes stored in body[] (≤ BODY_BUF_SIZE).

#if PC_ENABLE_STREAM_BODY
    proto_bool body_streaming; ///< True when the body is streamed to a sink, not buffered (OTA / upload).
#endif
} HttpReq;

/** @brief Pool of parser contexts, one per connection-pool slot (incl. reserved dispatch slots). */
extern HttpReq http_pool[CONN_POOL_SLOTS];

#if PC_ENABLE_STREAM_BODY
// ---------------------------------------------------------------------------
// Streaming-body hooks (OTA / file upload) - gated by PC_ENABLE_STREAM_BODY.
//
// When set, the parser consults @ref HttpStreamBeginCb at end-of-headers (the
// request line + all headers are parsed, so method/path/Authorization are
// available). If it returns true, the body is streamed to @ref HttpStreamDataCb
// in BODY_BUF_SIZE chunks instead of being buffered into body[] (and the
// BODY_BUF_SIZE / 413 cap is bypassed), enabling multi-MB uploads such as a
// firmware image fed to the ESP32 Update API or a file written to LittleFS. The
// matching route handler still runs at PARSE_COMPLETE to send the response.
// ---------------------------------------------------------------------------

/** @brief Decide whether to stream this request's body; begin the sink if so. */
typedef proto_bool (*HttpStreamBeginCb)(HttpReq *req);
/** @brief Receive one body chunk for a streamed request (@p req identifies the connection). */
typedef void (*HttpStreamDataCb)(HttpReq *req, const uint8_t *data, size_t len);
/**
 * @brief A streamed request was torn down before PARSE_COMPLETE (peer reset,
 * timeout, parse error). Lets the sink release its resource (close the file,
 * abort the Update) so a half-sent upload never leaks a handle.
 */
typedef void (*HttpStreamAbortCb)(HttpReq *req);

/** @brief Install the streaming-body hooks (pass NULL to disable; NULL abort if none is wanted). */
void http_parser_set_stream_hooks(HttpStreamBeginCb begin, HttpStreamDataCb data, HttpStreamAbortCb abort);
#endif // PC_ENABLE_STREAM_BODY

// ---------------------------------------------------------------------------
// Parser API
// ---------------------------------------------------------------------------

/**
 * @brief Reset a parser context to the initial (PARSE_METHOD) state.
 *
 * Zeroes all fields and sets `parse_state = PARSE_METHOD`.  Call before the
 * first use, after each completed or failed request, and on connection events.
 *
 * @param req  Parser context to reset.  Must not be null.
 */
void http_parser_reset(HttpReq *req);

/**
 * @brief Feed one byte to the parser state machine.
 *
 * Returns immediately without modifying state when `parse_state` is already
 * `PARSE_COMPLETE`, `PARSE_ERROR`, `PARSE_ENTITY_TOO_LARGE`, or
 * `PARSE_URI_TOO_LONG`.
 *
 * @param req  Parser context for this request.
 * @param byte Next byte from the HTTP stream.
 */
void http_parser_feed(HttpReq *req, uint8_t byte);

/**
 * @brief Look up a header value by name (case-insensitive).
 *
 * @param req  Parsed request.
 * @param key  Header field name (e.g. `"Content-Type"`).
 * @return Pointer to the null-terminated value, or `nullptr` if not found.
 */
const char *http_get_header(const HttpReq *req, const char *key);

/**
 * @brief Read a named cookie from the request `Cookie` header (RFC 6265 4.2.1).
 *
 * Parses the `name1=value1; name2=value2` list and copies the value of cookie
 * @p name (case-sensitive) into @p out (null-terminated, bounded by @p out_size;
 * a surrounding DQUOTE pair is stripped). Pairs with the session / CSRF / auth
 * features (e.g. reading a session-id cookie).
 *
 * @return true if the cookie was found (value in @p out), false otherwise.
 */
proto_bool http_get_cookie(const HttpReq *req, const char *name, char *out, size_t out_size);

/**
 * @brief Recover the original client from a reverse-proxy `Forwarded` (RFC 7239)
 *        or de-facto `X-Forwarded-For` / `X-Forwarded-Proto` header.
 *
 * Writes the leftmost (original-client) address into @p ip_out as its RFC 5952
 * canonical text (bounded by @p ip_cap; use ::PC_IP_STR_MAX for the widest IPv6),
 * and sets @p is_https from `proto=https` / `X-Forwarded-Proto: https`. Both IPv4
 * (with an optional `:port`) and IPv6 (bracketed `for="[2001:db8::1]:port"` or a
 * bare `X-Forwarded-For` literal) are recovered; the candidate is validated with
 * pc_ip_parse, so `unknown` / obfuscated `_id` identifiers and malformed tokens
 * are rejected. The CALLER must only trust this when the TCP peer is a configured
 * trusted upstream - the header is client-spoofable.
 *
 * @return true if a valid client address (IPv4 or IPv6) was written to @p ip_out.
 */
proto_bool http_forwarded_client(const HttpReq *req, char *ip_out, size_t ip_cap, proto_bool *is_https);

/**
 * @brief Look up a query parameter value by name (case-sensitive).
 *
 * @param req  Parsed request.
 * @param key  Parameter name.
 * @return Pointer to the null-terminated value (empty string if `key=` with
 *         no value), or `nullptr` if the key is absent.
 */
const char *http_get_query(const HttpReq *req, const char *key);

/**
 * @brief Look up an `application/x-www-form-urlencoded` body field by name.
 *
 * Parses the request body on demand (no extra per-request storage) when the
 * `Content-Type` is `application/x-www-form-urlencoded`, and copies the raw
 * (not percent-decoded, matching http_get_query()) value of @p key into
 * @p out. Useful for classic HTML form POSTs.
 *
 * @param req      Parsed request (body must be buffered, i.e. not streamed).
 * @param key      Field name (case-sensitive).
 * @param out      Caller buffer; always null-terminated on a true return.
 * @param out_size Size of @p out in bytes (must be >= 1).
 * @return `true` and fills @p out if the field is present; `false` otherwise
 *         (out is set to an empty string).
 */
proto_bool http_get_form(const HttpReq *req, const char *key, char *out, size_t out_size);

/**
 * @brief Look up a captured path parameter by name (case-sensitive).
 *
 * Path parameters are the `:name` segments of a matched route pattern
 * (e.g. route `"/users/:id"` matching `"/users/42"` captures `id`→`"42"`).
 * Populated by the dispatcher when the route matches; valid for the duration
 * of the handler.
 *
 * @param req  Parsed request.
 * @param key  Parameter name without the leading `:`.
 * @return Pointer to the null-terminated value, or `nullptr` if absent.
 */
const char *http_get_param(const HttpReq *req, const char *key);

PROTO_END_DECLS

#endif
