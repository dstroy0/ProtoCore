// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file presentation.h
 * @brief Layer 6 (Presentation) - wires the transport ring buffer to the HTTP parser.
 *
 * This layer owns two responsibilities:
 *   1. Hold HTTP's own per-slot state - the request tally, the request deadline, the response sink,
 *      and the h2/h3 fields - keyed on the transport slot index.
 *   2. Expose ::HttpConn, the calls the session layer dispatches through and the application layer
 *      drives by slot.
 *
 * The parsing itself lives in `http_parser.h`, which this includes so callers need one include.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PRESENTATION_H
#define PROTOCORE_PRESENTATION_H

#include "network_drivers/session/session.h" // the per-connection tables this reads
#include "../transport/tcp/evt.h" // EvtType: the event a handler is dispatched on
#include "network_drivers/presentation/http/http_parser/http_parser.h"

// ---------------------------------------------------------------------------
// Slot-indexed wrappers called by the session and application layers
// ---------------------------------------------------------------------------

/**
 * @brief Reset the HTTP parser for a connection slot.
 *
 * Delegates to http_parser_reset() for the slot's request struct.
 * Silently ignores out-of-range slot IDs.
 *
 * @param slot_id Index into conn_pool / http_pool (0 … MAX_CONNS-1).
 */

#if PROTOCORE_ENABLE_KEEPALIVE
/**
 * @brief Requests served on each connection slot (HTTP keep-alive fairness bound).
 *
 * Reset to 0 by http_conn_open() when a connection is accepted; incremented by
 * keepalive_eval() per response it elects to keep alive. Lives here (not in
 * TcpConn) so the transport layer stays free of HTTP semantics. Defined in
 * presentation.c.
 */
extern uint16_t http_req_count[MAX_CONNS];
#endif

/**
 * @brief Self-framing response sink (Layer 5 TX seam).
 *
 * HTTP/2 installs it at ALPN, HTTP/3 at dispatch, so the response methods route through it instead
 * of building an HTTP/1.1 message. Null means plain HTTP/1.1, the default builder.
 */

/**
 * @brief HTTP's own per-slot state, keyed on the transport slot index.
 *
 * All of it HTTP semantics, so it lives here rather than in TcpConn, the same way
 * ::http_req_count already does. Sized CONN_POOL_SLOTS, not MAX_CONNS: the HTTP/3 dispatch slot is
 * a reserved index above the TCP range. Defined in presentation.c.
 *
 * @var http_req_start_ms  protocore_millis() at the first byte of the in-progress request (0 = none).
 *                         The request-header deadline (PROTOCORE_REQUEST_TIMEOUT_MS, slow-loris
 *                         defense) measures against this; unlike the transport's idle timer a
 *                         trickle byte cannot reset it. Armed by the HTTP layer on the first byte.
 * @var http_resp_sink     the TX seam above, per slot.
 */

#if PROTOCORE_ENABLE_KEEPALIVE

/**
 * @brief Whether the connection carrying @p slot_id's request is reused for the next one.
 *
 * Reads the parsed request: a message whose boundary is not known closes, HTTP/1.1 is persistent
 * unless Connection carries "close", and 1.0 is the reverse. A kept connection counts against
 * PROTOCORE_KEEPALIVE_MAX_REQUESTS and closes once it reaches the bound.
 */
#endif

#if PROTOCORE_ENABLE_KEEPALIVE || PROTOCORE_ENABLE_WEBSOCKET
/**
 * @brief Whether @p token appears as an element of the Connection header value @p hdr.
 *
 * The value is a comma-delimited list ("Keep-Alive, Upgrade"), matched case insensitively on whole
 * elements so a longer token cannot match on its prefix.
 */
#endif

/**
 * @brief Initialize a slot for a freshly-accepted HTTP connection.
 *
 * Resets the HTTP parser (like http_reset()) and, when keep-alive is enabled,
 * zeroes the slot's persistent request counter. The session layer calls this on
 * EvtType::EVT_CONNECT; http_reset() is used for the lighter inter-request reset that must
 * not clear the counter. With keep-alive off this is identical to http_reset().
 *
 * @param slot_id Index into conn_pool / http_pool (0 … MAX_CONNS-1).
 */

/**
 * @brief Drain the transport ring buffer and advance the HTTP parser.
 *
 * Reads all available bytes from the slot's transport ring buffer and feeds
 * each byte to `http_parser_feed()`.  Stops early if the parser reaches a
 * terminal state (PARSE_COMPLETE, PARSE_ERROR, PARSE_ENTITY_TOO_LARGE,
 * PARSE_URI_TOO_LONG).
 *
 * Silently ignores out-of-range slot IDs.
 *
 * @param slot_id Connection slot to parse.
 */

/**
 * @brief The HTTP connection ProtoHandler (the L5 dispatch seam).
 *
 * The accept/data/close handlers - the data path multiplexes the TLS handshake,
 * HTTP/2 ALPN, and the WebSocket upgrade before the HTTP/1.1 parser. Returned by
 * accessor (not self-registered) so this module carries no dependency on the
 * session layer; Session.proto->register_builtins() installs it.
 */
struct ProtoHandler;

/** @brief RFC 9110 sec 5.6.1: a comma-separated header, and the token looked for in it. */
typedef struct
{
    const char *hdr;   ///< the field value scanned
    const char *token; ///< the token looked for, case-insensitive
} HttpHdrArgs;

/** @brief The HTTP connection glue's own state and the calls that reach it, described only in presentation.c. */
struct HttpConnInternal;

/**
 * @brief Layer 6 - the HTTP connection: what wires a transport slot to the HTTP parser.
 *
 * A caller sets the members a call takes, invokes it through ::HttpConn, and reads the outcome off
 * the same handle.
 *
 * @var HttpConnNs::slot      the connection a call acts on
 * @var HttpConnNs::hdr_args  the header value a token test reads, and the element it looks for
 * @var HttpConnNs::poll      the per-slot poll pump the application installs
 * @var HttpConnNs::ok        a call's true/false outcome
 * @var HttpConnNs::handler   the ProtoHandler a lookup reports
 * @var HttpConnNs::reset       reset the parser between requests, leaving the keep-alive tally
 * @var HttpConnNs::conn_open   initialize a slot for a freshly-accepted connection
 * @var HttpConnNs::parse       drain the slot's bytes and advance the parser
 * @var HttpConnNs::keepalive_eval  whether the connection is reused for the next request
 * @var HttpConnNs::has_token   whether hdr_args.token appears as an element of hdr_args.hdr
 * @var HttpConnNs::proto_handler  the L5 dispatch seam this module registers into
 * @var HttpConnNs::set_poll    install the per-slot poll pump
 * @var HttpConnNs::internal    the glue's state and the calls that reach it
 */
typedef struct
{
    uint8_t slot;               ///< the connection every call names
    void (*poll)(uint8_t slot); ///< what set_poll installs as the per-tick step

    HttpHdrArgs hdr_args; ///< the header a token scan reads

    proto_bool ok;
    const struct ProtoHandler *handler;

    void (*reset)(struct HttpConnInternal *ctx);
    void (*conn_open)(struct HttpConnInternal *ctx);
    void (*parse)(struct HttpConnInternal *ctx);
#if PROTOCORE_ENABLE_KEEPALIVE
    void (*keepalive_eval)(struct HttpConnInternal *ctx);
#endif
#if PROTOCORE_ENABLE_KEEPALIVE || PROTOCORE_ENABLE_WEBSOCKET
    void (*has_token)(struct HttpConnInternal *ctx);
#endif
    void (*proto_handler)(struct HttpConnInternal *ctx);
    void (*set_poll)(struct HttpConnInternal *ctx);

    struct HttpConnInternal *internal;
} HttpConnNs;

/** @brief The one symbol this module exports. */
extern HttpConnNs HttpConn;

#endif
