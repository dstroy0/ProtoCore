// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file presentation.h
 * @brief Layer 6 (Presentation) - wires the transport ring buffer to the HTTP parser.
 *
 * This layer owns two responsibilities:
 *   1. Drain bytes via the transport read API (pc_conn_available / pc_conn_read_byte)
 *      and feed them into the HTTP parser one at a time - the ring is transport's.
 *   2. Expose slot-indexed `http_reset()` and `http_parse()` helpers that the
 *      session layer (server_tick) and application layer (handle) call by slot ID.
 *
 * The actual HTTP parsing logic lives in `http_parser.h / http_parser.cpp`.
 * This file merely includes it so callers only need one include.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PRESENTATION_H
#define PROTOCORE_PRESENTATION_H

#include "../transport/tcp.h"
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
void http_reset(uint8_t slot_id);

#if PC_ENABLE_KEEPALIVE
/**
 * @brief Requests served on each connection slot (HTTP keep-alive fairness bound).
 *
 * Reset to 0 by http_conn_open() when a connection is accepted; incremented by
 * the application layer per response it elects to keep alive. Lives here (not in
 * TcpConn) so the transport layer stays free of HTTP semantics. Defined in
 * presentation.cpp.
 */
extern uint16_t http_req_count[MAX_CONNS];
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
void http_conn_open(uint8_t slot_id);

/**
 * @brief Drain the transport ring buffer and advance the HTTP parser.
 *
 * Reads all available bytes from the slot's transport ring buffer and feeds
 * each byte to `http_parser_feed()`.  Stops early if the parser reaches a
 * terminal state (ParseState::PARSE_COMPLETE, ParseState::PARSE_ERROR, ParseState::PARSE_ENTITY_TOO_LARGE,
 * ParseState::PARSE_URI_TOO_LONG).
 *
 * Silently ignores out-of-range slot IDs.
 *
 * @param slot_id Connection slot to parse.
 */
void http_parse(uint8_t slot_id);

/**
 * @brief The HTTP connection ProtoHandler (the L5 dispatch seam).
 *
 * The accept/data/close handlers - the data path multiplexes the TLS handshake,
 * HTTP/2 ALPN, and the WebSocket upgrade before the HTTP/1.1 parser. Returned by
 * accessor (not self-registered) so this module carries no dependency on the
 * session layer; proto_register_builtins() installs it.
 */
struct ProtoHandler;
const struct ProtoHandler *http_proto_handler(void);

/**
 * @brief Install the HTTP per-slot poll pump (the routing core's instance-bound `on_poll`).
 *
 * HTTP is the one protocol whose poll needs the `PC` instance (routing), so the
 * application layer installs its pump here at `begin()` - the TX-seam (`pc_resp_sink`) counterpart
 * for the poll direction. With it set, HTTP plugs into the uniform `ProtoHandler::on_poll` seam
 * exactly like every other protocol, so the L5/worker dispatch loop has no HTTP special case.
 */
void http_proto_set_poll(void (*fn)(uint8_t slot));

#if PC_ENABLE_EDGE_CACHE
/**
 * @brief Install the edge-cache per-slot fetch pump (defined in protocore.cpp, called first in
 *        http_poll_slot).
 *
 * A cache miss / stale-entry revalidation suspends the client request and drives a non-blocking origin
 * fetch from the slot's poll. @p fn returns true while it is servicing an in-flight fetch for the slot,
 * so the HTTP pipeline is skipped until the fetch completes and starts the cached response. Nullable
 * (unset = no-op). Installed by pc_edge_cache_enable(); see services/web/edge_cache/edge_cache_proxy.
 */
void pc_http_set_edge_poll(bool (*fn)(uint8_t slot));
#endif

#endif
