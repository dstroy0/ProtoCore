// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_client.h
 * @brief Layer 7 - the user agent (RFC 9110 sec 3.5): one outbound HTTP/1.1 message exchange.
 *
 * RFC 9110 sec 3.5: "the term 'user agent' refers to any of the various client programs that
 * initiate a request." This is that side of HTTP. The serving side is network_drivers/presentation/http.
 *
 * The exchange runs in three steps, each a call on ::HttpClient:
 *
 *  - @ref HttpClientNs::parse_target_uri splits the target URI (RFC 9110 sec 7.1) into the authority
 *    it dials and the origin-form request target it sends (RFC 9112 sec 3.2.1).
 *  - @ref HttpClientNs::build_request writes the request-line and field lines of an HTTP-message
 *    (RFC 9112 sec 2.1, sec 3).
 *  - @ref HttpClientNs::parse_response reads the status-line (RFC 9112 sec 4) and frames the message
 *    body by the rules of RFC 9112 sec 6.3.
 *
 * Those three touch no socket and are unit-tested on the host. @ref HttpClientNs::get and
 * @ref HttpClientNs::post run all three over the shared outbound transport (::TcpClient), with the
 * TLS record layer under them for an https target URI (RFC 9112 sec 9.7).
 *
 * The content is returned by pointer into the module's receive buffer and is valid until the next
 * call. One exchange at a time: the module holds one connection and one buffer.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HTTP_CLIENT_H
#define PROTOCORE_HTTP_CLIENT_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP_CLIENT

PROTOCORE_BEGIN_DECLS

/**
 * @brief What went wrong below the status code.
 *
 * RFC 9110 sec 15: every status code is in the range 100..599, so a negative value can never
 * collide with one and @ref HttpClientNs::status carries both.
 */
typedef enum PROTO_ENUM_PACKED
{
    HTTP_CLIENT_ERR_URL = -1,      ///< the target URI does not parse (RFC 9110 sec 4.2.1 / 4.2.2)
    HTTP_CLIENT_ERR_DNS = -2,      ///< the uri-host did not resolve; the transport reports that as a close, so an
                                   ///< exchange that fails to resolve reports ::HTTP_CLIENT_ERR_CONNECT
    HTTP_CLIENT_ERR_CONNECT = -3,  ///< the connection did not come up (RFC 9112 sec 9.1)
    HTTP_CLIENT_ERR_TIMEOUT = -4,  ///< no complete message arrived before the deadline
    HTTP_CLIENT_ERR_SEND = -5,     ///< the request message did not go out
    HTTP_CLIENT_ERR_RESPONSE = -6, ///< the response does not parse (RFC 9112 sec 4)
    HTTP_CLIENT_ERR_TLS = -7,      ///< an https target URI with no TLS, or a failed handshake
} HttpClientError;

/**
 * @brief RFC 9110 sec 7.1: the target URI, split into the parts a request sends.
 *
 * A parse writes @c host, @c path, @c port and @c https out of @c url; a build reads them back.
 * Scheme defaults are RFC 9110 sec 4.2.1 (http, TCP port 80) and sec 4.2.2 (https, TCP port 443).
 */
typedef struct
{
    const char *url;  ///< the absolute target URI, "http://..." or "https://..."
    char *host;       ///< where the uri-host goes, and what the Host field carries (RFC 9110 sec 7.2)
    size_t host_cap;  ///< how much room it has
    char *path;       ///< where the origin-form request target goes (RFC 9112 sec 3.2.1)
    size_t path_cap;  ///< how much room it has
    uint16_t port;    ///< the authority's port; 80 for http, 443 for https when the URI omits it
    proto_bool https; ///< the scheme is "https", so the exchange runs over TLS (RFC 9112 sec 9.7)
} HttpTargetArgs;

/** @brief RFC 9112 sec 3: the request-line's method, the content it encloses, and where it is built. */
typedef struct
{
    const char *method;       ///< the method token (RFC 9112 sec 3.1), "GET" or "POST" here
    const char *content_type; ///< the Content-Type field value (RFC 9110 sec 8.3); null takes application/octet-stream
    const uint8_t *body;      ///< the content the message encloses, or null
    size_t body_len;          ///< its octet count, sent as Content-Length (RFC 9110 sec 8.6)
    char *out;                ///< where the request message is written
    size_t cap;               ///< how much room it has
} HttpRequestArgs;

/** @brief RFC 9112 sec 2.1: the received message a parse frames. Chunked decoding rewrites it. */
typedef struct
{
    uint8_t *buf; ///< the octets received, start-line first
    size_t len;   ///< how many
} HttpMessageArgs;

/**
 * @brief The HTTP user agent (RFC 9110 sec 3.5).
 *
 * A caller sets the members a call takes, invokes it through ::HttpClient, and reads the outcome off
 * the same handle. There is no slot member: the module runs one exchange at a time, so no call names
 * a row.
 *
 * An exchange sets @c target.host, @c target.path and @c request.out to the module's own buffers
 * before it splits the target URI, so a caller sets only @c target.url and reads the parts back.
 *
 * @var HttpClientNs::target   the target URI a call splits, dials, or names in a request-line
 * @var HttpClientNs::request  the method, the content, and where the request message is built
 * @var HttpClientNs::message  the received message a parse frames
 * @var HttpClientNs::ok       a call's true/false outcome
 * @var HttpClientNs::status   the status-code (RFC 9112 sec 4), or a negative ::HttpClientError
 * @var HttpClientNs::n        octets a build wrote; 0 when the message would not fit
 * @var HttpClientNs::body_off where the content starts inside the parsed message
 * @var HttpClientNs::body_len the content's octet count (RFC 9112 sec 6.3)
 * @var HttpClientNs::body     the content an exchange read, pointing into the module's receive buffer
 * @var HttpClientNs::parse_target_uri  split the target URI into authority, port and request target
 * @var HttpClientNs::build_request     write the request-line and field lines of one HTTP-message
 * @var HttpClientNs::parse_response    read the status-line and frame the message body
 * @var HttpClientNs::get               run one GET exchange (RFC 9110 sec 9.3.1)
 * @var HttpClientNs::post              run one POST exchange (RFC 9110 sec 9.3.3)
 */
typedef struct
{
    HttpTargetArgs target;   ///< the target URI and its parts (RFC 9110 sec 7.1)
    HttpRequestArgs request; ///< what a request-line and its field lines carry (RFC 9112 sec 3)
    HttpMessageArgs message; ///< the received message a parse frames (RFC 9112 sec 2.1)
    proto_bool ok;
    int32_t status;
    size_t n;
    size_t body_off;
    size_t body_len;
    const uint8_t *body;
} HttpClientVars;

/** @brief The operands and the outcome. */
extern HttpClientVars HttpClientV;

/** @brief The entries. */
typedef struct
{
    void (*const parse_target_uri)(uint8_t *restrict work);
    void (*const build_request)(uint8_t *restrict work);
    void (*const parse_response)(uint8_t *restrict work);
    void (*const get)(uint8_t *restrict work);
    void (*const post)(uint8_t *restrict work);
} HttpClientNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HttpClientV or a region of the borrow at a fixed offset.
void protocore_http_client_parse_target_uri(uint8_t *restrict work);
void protocore_http_client_build_request(uint8_t *restrict work);
void protocore_http_client_parse_response(uint8_t *restrict work);
void protocore_http_client_get(uint8_t *restrict work);
void protocore_http_client_post(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `HttpClient.parse_target_uri(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HttpClientNs HttpClient __attribute__((unused)) = {
    .parse_target_uri = protocore_http_client_parse_target_uri,
    .build_request = protocore_http_client_build_request,
    .parse_response = protocore_http_client_parse_response,
    .get = protocore_http_client_get,
    .post = protocore_http_client_post,
};

/**
 * @brief The PROTOCORE_HTTP_CLIENT_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
#if PROTOCORE_HAS_NET_STACK
uint8_t *protocore_http_client_span(void);
#endif // PROTOCORE_HAS_NET_STACK

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_CLIENT

#endif // PROTOCORE_HTTP_CLIENT_H
