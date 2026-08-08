// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "network_drivers/presentation/http/http.h" // HttpMethod and Handler: what a row dispatches on
#include "protocore_config.h"                       // the entry point: MAX_ROUTES, and the widths

PROTO_BEGIN_DECLS

/** @brief Discriminates between HTTP, WebSocket, and SSE route entries. */
typedef enum
{
    ROUTE_HTTP, ///< Standard HTTP request/response.
#if PC_ENABLE_WEBSOCKET
    ROUTE_WS, ///< WebSocket upgrade route.
#endif
#if PC_ENABLE_SSE
    ROUTE_SSE, ///< Server-Sent Events route.
#endif
#if PC_ENABLE_FILE_SERVING
    ROUTE_STATIC, ///< Static-file subtree mount (serve_static()).
#endif
#if PC_ENABLE_WEBDAV
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

#if PC_ENABLE_WEBSOCKET
    /// The handler set this route serves, or PC_WS_NONE. The handlers belong to the websocket
    /// module: a route decides where a request goes, and what runs once the socket is open is not
    /// routing's business.
    uint8_t ws_id;
#endif

#if PC_ENABLE_SSE
    /// The handler this route serves, or PC_SSE_NONE. The handler belongs to the sse module: a
    /// route decides where a request goes, not what runs once a client subscribes.
    uint8_t sse_id;
#endif

#if PC_ENABLE_FILE_SERVING
    /// The mount point this route serves, or PC_MNT_NONE. The backend and subtree belong to mnt:
    /// two registrars describe a mount the same way, so the description lives with mounting.
    uint8_t mnt_id;
#endif

#if PC_ENABLE_AUTH
    /// The credential set this route needs, or PC_AUTH_NONE. The credentials themselves belong to
    /// the auth module: a route decides where a request goes, and key material in a routing entry is
    /// a copy of a secret in a place that has no reason to hold one.
    uint8_t auth_id; ///< Required password.
#endif

    proto_bool is_active;    ///< `false` for unused table slots.
    proto_bool is_wildcard;  ///< `true` when path ends with `*`.
    proto_bool is_param;     ///< `true` when the path contains a `:name` segment.
    proto_bool is_regex;     ///< `true` when the path is a regex (see on_regex()).
    pc_if_kind iface_filter; ///< Interface gate; PC_IF_ANY (0) = match any interface.
} HttpRoute;

/** @brief The table's storage. Declared, never defined here: the layout stays in route.c. */
typedef struct HttpRouteCtx HttpRouteCtx;

/**
 * @brief The route-table module.
 *
 * @var HttpRouteNs::add    take the next free entry, zeroed and ready to fill, or NULL when full. The
 *                      first one borrows the table from the secure pool.
 * @var HttpRouteNs::count  entries currently registered.
 * @var HttpRouteNs::at     entry @c i, or NULL if @c i is past the end.
 * @var HttpRouteNs::reset  empty the table. For tests: a case that does not reset matches against every
 *                      route the previous cases registered.
 *
 * The storage handle is not a member. The borrow is taken at the first @ref HttpRouteNs::add and this
 * object is `const`, so a member could only ever hold an address settled before the program did.
 * Nothing above this module needs the handle: a caller takes an entry or walks by index.
 */
typedef struct
{
    HttpRoute *(*add)(void);
    uint8_t (*count)(void);
    HttpRoute *(*at)(uint8_t i);
    void (*reset)(void);
} HttpRouteNs;

/** @brief The one symbol this module exports. */
extern const HttpRouteNs HttpRoutes;

PROTO_END_DECLS

#endif // PROTOCORE_HTTP_ROUTE_H
