// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file route.h
 * @brief The route table: where a request goes.
 *
 * A row names a path pattern, a method, a handler, and the ws / sse / mount / credential id the
 * request needs. Every one of those is HTTP, so the table sits under the HTTP root rather than at
 * the network layer, which routes datagrams.
 *
 * The module exports one symbol, @ref HttpRoutes. Everything in route.c has internal linkage.
 */

#ifndef PROTOCORE_HTTP_ROUTE_H
#define PROTOCORE_HTTP_ROUTE_H

#include "network_drivers/presentation/http/http.h" // the complete type a public struct below holds by value
#include "protocore_config.h"                       // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP_ROUTE

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Discriminates between HTTP, WebSocket, and SSE route entries. */
typedef enum
{
    ROUTE_HTTP, ///< Standard HTTP request/response.
#if PROTOCORE_ENABLE_WEBSOCKET
    ROUTE_WS, ///< WebSocket upgrade route.
#endif
#if PROTOCORE_ENABLE_SSE
    ROUTE_SSE, ///< Server-Sent Events route.
#endif
#if PROTOCORE_ENABLE_FILE_SERVING
    ROUTE_STATIC, ///< Static-file subtree mount (serve_static()).
#endif
#if PROTOCORE_ENABLE_WEBDAV
    ROUTE_DAV, ///< WebDAV subtree mount (dav()).
#endif
} HttpRouteType;

/**
 * @brief Internal route entry stored in the routing table.
 *
 * Populated by on(), on_ws(), or on_sse().
 * Application code does not interact with this struct directly.
 */
typedef struct HttpRoute
{
    char path[MAX_PATH_LEN]; ///< Null-terminated path pattern.
    HttpRouteType type;      ///< HTTP, WS, or SSE.
    HttpMethod method;       ///< HTTP method (ROUTE_HTTP only).
    Handler callback;        ///< HTTP handler (ROUTE_HTTP only).

#if PROTOCORE_ENABLE_WEBSOCKET
    /// The handler set this route serves, or PROTOCORE_WS_NONE. The handlers belong to the websocket
    /// module: a route decides where a request goes, and what runs once the socket is open is not
    /// routing's business.
    uint8_t ws_id;
#endif

#if PROTOCORE_ENABLE_SSE
    /// The handler this route serves, or PROTOCORE_SSE_NONE. The handler belongs to the sse module: a
    /// route decides where a request goes, not what runs once a client subscribes.
    uint8_t sse_id;
#endif

#if PROTOCORE_ENABLE_FILE_SERVING
    /// The mount point this route serves, or PROTOCORE_MNT_NONE. The backend and subtree belong to mnt:
    /// two registrars describe a mount the same way, so the description lives with mounting.
    uint8_t mnt_id;
#endif

#if PROTOCORE_ENABLE_AUTH
    /// The credential set this route needs, or PROTOCORE_AUTH_NONE. The credentials themselves belong to
    /// the auth module: a route decides where a request goes, and key material in a routing entry is
    /// a copy of a secret in a place that has no reason to hold one.
    uint8_t auth_id; ///< Required password.
#endif

    proto_bool is_active;           ///< `false` for unused table slots.
    proto_bool is_wildcard;         ///< `true` when path ends with `*`.
    proto_bool is_param;            ///< `true` when the path contains a `:name` segment.
    proto_bool is_regex;            ///< `true` when the path is a regex (see on_regex()).
    protocore_if_kind iface_filter; ///< Interface gate; PROTOCORE_IF_ANY (0) = match any interface.
} HttpRoute;

/** @brief The table's storage. Declared, never defined here: the layout stays in route.c. */
typedef struct HttpRouteCtx HttpRouteCtx;

/** @brief What at takes: i. */
typedef struct
{
    uint8_t i;
} HttpRoutesAtArgs;
/**
 * @brief The route table: where a request goes.
 *
 * A caller sets the members a call takes, invokes it through ::HttpRoutes with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   HttpRoutes.add(work);
 *   // HttpRoutes.ptr is what the call reports
 *
 * @var HttpRouteNs::at_args  what at takes: i
 * @var HttpRouteNs::ok  a call's true/false outcome
 * @var HttpRouteNs::ptr  the pointer a call reports
 * @var HttpRouteNs::value  the value a call reports
 * @var HttpRouteNs::add  take the next free entry, zeroed and ready to fill, or NULL when ...
 * @var HttpRouteNs::count  entries currently registered
 * @var HttpRouteNs::at  entry i, or NULL if i is past the end
 * @var HttpRouteNs::reset  empty the table. For tests: a case that does not reset matches ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    HttpRoutesAtArgs at_args;
    proto_bool ok;
    HttpRoute *ptr;
    uint8_t value;
} HttpRoutesVars;

/** @brief The operands and the outcome. */
extern HttpRoutesVars HttpRoutesV;

/** @brief The entries. */
typedef struct
{
    void (*const add)(uint8_t *restrict work);
    void (*const count)(uint8_t *restrict work);
    void (*const at)(uint8_t *restrict work);
    void (*const reset)(uint8_t *restrict work);
} HttpRouteNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HttpRoutesV or a region of the borrow at a fixed offset.
void protocore_http_route_add(uint8_t *restrict work);
void protocore_http_route_count(uint8_t *restrict work);
void protocore_http_route_at(uint8_t *restrict work);
void protocore_http_route_reset(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `HttpRoutes.add(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HttpRouteNs HttpRoutes __attribute__((unused)) = {
    .add = protocore_http_route_add,
    .count = protocore_http_route_count,
    .at = protocore_http_route_at,
    .reset = protocore_http_route_reset,
};

/**
 * @brief The bytes every entry here runs out of: the one route table.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where that
 * borrow comes from. Every registrar and every reader drives the same table, so the bytes belong to
 * this module rather than to any one caller. Taken once from the end of the secure pool, which no
 * mark and no release walks, so the table lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_http_route_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_ROUTE

#endif // PROTOCORE_HTTP_ROUTE_H
