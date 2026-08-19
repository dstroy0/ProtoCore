// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http.h
 * @brief The HTTP root: the parts of the protocol that do not belong to one version.
 *
 * A status code, a method token, a route path match and an Allow list read the same on HTTP/1.1,
 * HTTP/2 and HTTP/3, so they sit here and each version module calls them.
 *
 * The one symbol this module exports is @ref Http.
 */

#ifndef PROTOCORE_HTTP_H
#define PROTOCORE_HTTP_H

#include "network_drivers/presentation/http/http_parser/http_parser.h"

PROTOCORE_BEGIN_DECLS

/** @brief The request methods a route binds to. */
typedef enum
{
    HTTP_GET,           ///< Safe, idempotent read
    HTTP_POST,          ///< Non-idempotent create / action
    HTTP_PUT,           ///< Idempotent replace
    HTTP_DELETE,        ///< Idempotent delete
    HTTP_PATCH,         ///< Partial update
    HTTP_HEAD,          ///< Same as GET but no response body
    HTTP_OPTIONS,       ///< Capability query / CORS preflight
    HTTP_METHOD_UNKNOWN ///< Unrecognized method token, answered 501
} HttpMethod;

/** @brief A route's request handler. */
typedef void (*Handler)(uint8_t slot_id, HttpReq *request);

/** @brief RFC 9110 sec 9: a method, held either as the enum or as its token. */
typedef struct
{
    HttpMethod method; ///< the method a name lookup names
    const char *token; ///< a method token, or the one an Allow list appends
} HttpMethodArgs;

/** @brief RFC 9110 sec 7.1 request target: the route pattern, and the path tested against it. */
typedef struct
{
    const char *route;      ///< the route path a match tests
    proto_bool is_wildcard; ///< that route ends in a trailing star
    const char *path;       ///< the request path a match tests against
    HttpReq *req;           ///< the request a param capture writes into
} HttpRouteArgs;

/** @brief RFC 9110 sec 10.2.1 Allow: the comma-separated list an append builds. */
typedef struct
{
    char *buf;  ///< where the list is built
    size_t cap; ///< how much room it has
} HttpAllowArgs;

/**
 * @brief The version-agnostic HTTP surface.
 *
 * A caller sets the members a call takes, invokes it through ::Http, and reads the outcome off the
 * same handle.
 *
 * @var HttpNs::slot        the connection a call acts on
 * @var HttpNs::code        the status code a lookup names
 * @var HttpNs::method_args a method, as an enum or as a token
 * @var HttpNs::route_args  the target a route is matched against
 * @var HttpNs::allow       the Allow list an append builds
 * @var HttpNs::cb          the handler a request runs when no route matched
 * @var HttpNs::edge_poll   the edge-cache origin fetch that owns a slot while it is in flight
 * @var HttpNs::ok          a call's true/false outcome
 * @var HttpNs::text        the reason phrase or method token a lookup reports
 * @var HttpNs::method_of   the enum a token parses to
 * @var HttpNs::status_text        the reason phrase for @c code; "Unknown" for one it has none for
 * @var HttpNs::parse_method       the enum for @c method_args.token, HTTP_METHOD_UNKNOWN for an unimplemented one
 * @var HttpNs::method_name        the canonical token for @c method_args.method, empty for HTTP_METHOD_UNKNOWN
 * @var HttpNs::path_matches       whether @c route_args.route matches @c route_args.path, exact or trailing star
 * @var HttpNs::match_path_params  whether a `:name` route matches, capturing each segment into @c route_args.req
 * @var HttpNs::req_is_head        whether the request on @c slot used HEAD
 * @var HttpNs::allow_append       add @c method_args.token to a comma-separated Allow list, skipping a repeat
 * @var HttpNs::match_and_execute  run a completed request on @c slot through the route table
 * @var HttpNs::set_not_found      install the handler a request runs when no route matched
 * @var HttpNs::poll_slot          the ProtoHandler on_poll for an HTTP slot: pumps, drains, dispatches
 * @var HttpNs::reset              drop every handler registered here, back to the built-in answers
 * @var HttpNs::set_edge_poll      install the edge-cache origin fetch
 */

typedef struct
{
    uint8_t slot; ///< the connection every call names
    int code;     ///< the status code a lookup names
    Handler cb;   ///< the handler a request runs when no route matched

    HttpMethodArgs method_args; ///< a method, as an enum or as a token
    HttpRouteArgs route_args;   ///< the target a route is matched against
    HttpAllowArgs allow;        ///< the Allow list an append builds
#if PROTOCORE_ENABLE_EDGE_CACHE
    proto_bool (*edge_poll)(uint8_t slot);
#endif

    proto_bool ok;
    const char *text;
    HttpMethod method_of;

    void (*const status_text)(uint8_t *restrict work);
    void (*const parse_method)(uint8_t *restrict work);
    void (*const method_name)(uint8_t *restrict work);
    void (*const path_matches)(uint8_t *restrict work);
    void (*const match_path_params)(uint8_t *restrict work);
    void (*const req_is_head)(uint8_t *restrict work);
    void (*const allow_append)(uint8_t *restrict work);
    void (*const match_and_execute)(uint8_t *restrict work);
    void (*const set_not_found)(uint8_t *restrict work);
    void (*const poll_slot)(uint8_t *restrict work);
    void (*const reset)(uint8_t *restrict work);
#if PROTOCORE_ENABLE_EDGE_CACHE
    void (*const set_edge_poll)(uint8_t *restrict work);
#endif
} HttpNs;

/** @brief The one symbol this module exports. */
extern HttpNs Http;

/**
 * @brief The PROTOCORE_HTTP_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_http_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_HTTP_H
