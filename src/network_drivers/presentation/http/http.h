// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

PROTO_BEGIN_DECLS

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

/**
 * @brief The version-agnostic HTTP surface.
 *
 * @var HttpNs::status_text        the reason phrase for a status code; "Unknown" for one it has none for
 * @var HttpNs::parse_method       the enum for a method token, HTTP_METHOD_UNKNOWN for an unimplemented one
 * @var HttpNs::method_name        the canonical token for a method, empty for HTTP_METHOD_UNKNOWN
 * @var HttpNs::path_matches       whether a route path matches a request path, exact or trailing star
 * @var HttpNs::match_path_params  whether a `:name` route matches, capturing each segment into the request
 * @var HttpNs::req_is_head        whether the request on a slot used HEAD
 * @var HttpNs::allow_append       add a method token to a comma-separated Allow list, skipping a repeat
 * @var HttpNs::match_and_execute  run a completed request on a slot through the route table
 * @var HttpNs::set_not_found      the handler a request runs when no route matched
 * @var HttpNs::poll_slot          the ProtoHandler on_poll for an HTTP slot: pumps, drains, dispatches
 * @var HttpNs::set_edge_poll      the edge-cache origin fetch that owns a slot while it is in flight
 */
typedef struct
{
    const char *(*status_text)(int code);
    HttpMethod (*parse_method)(const char *m);
    const char *(*method_name)(HttpMethod m);
    proto_bool (*path_matches)(const char *route, proto_bool is_wildcard, const char *req_path);
    proto_bool (*match_path_params)(const char *route, const char *path, HttpReq *req);
    proto_bool (*req_is_head)(uint8_t slot_id);
    void (*allow_append)(char *buf, size_t cap, const char *m);
    void (*match_and_execute)(uint8_t slot_id);
    void (*set_not_found)(Handler cb);
    void (*poll_slot)(uint8_t slot);
#if PC_ENABLE_EDGE_CACHE
    void (*set_edge_poll)(proto_bool (*fn)(uint8_t slot));
#endif
} HttpNs;

/** @brief The one symbol this module exports. */
extern const HttpNs Http;

PROTO_END_DECLS

#endif // PROTOCORE_HTTP_H
