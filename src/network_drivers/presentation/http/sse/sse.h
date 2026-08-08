// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sse.h
 * @brief Layer 6 (Presentation) -- Server-Sent Events connection pool.
 *
 * SSE (WHATWG HTML Living Standard, Server-sent events; the W3C EventSource
 * API) is a long-lived HTTP GET response with Content-Type: text/event-stream.
 * After the initial headers the connection stays open indefinitely; the server
 * pushes newline-delimited event records at any time.
 *
 * **Event record format** (WHATWG HTML, Server-sent events)
 * ```
 * [event: <name>\n]
 * [id: <id>\n]
 * data: <payload>\n
 * \n
 * ```
 *
 * Each SseConn occupies one TCP slot from conn_pool[] for the lifetime of
 * the subscription.  The total number of simultaneous SSE connections is
 * capped at MAX_SSE_CONNS.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSE_H
#define PROTOCORE_SSE_H

#include "network_drivers/transport/tcp.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Per-connection SSE state
// ---------------------------------------------------------------------------

/**
 * @brief SSE connection state stored in pc_sse_pool[].
 *
 * Allocated when the SSE handshake (200 + headers) is sent.  slot_id ties
 * this entry back to conn_pool[] and the underlying TCP PCB.
 */
typedef struct
{
    uint8_t pc_sse_id; ///< Index into pc_sse_pool[] (set at init).
    uint8_t slot_id;   ///< Owning TCP slot in conn_pool[].
    proto_bool active; ///< True when this entry is in use.

    /** Path this client subscribed to (for pc_sse_broadcast() matching). */
    char path[MAX_PATH_LEN];
} SseConn;

/** @brief Pool of SSE connection state, one per MAX_SSE_CONNS. */
extern SseConn pc_sse_pool[MAX_SSE_CONNS];

// ---------------------------------------------------------------------------
// SSE pool API
// ---------------------------------------------------------------------------

/**
 * @brief Callback fired when a new SSE client connects.
 *
 * Use pc_sse_send() inside this callback to push an initial event if needed.
 *
 * @param pc_sse_id  Index into pc_sse_pool[] for this connection.
 */
typedef void (*SseConnectHandler)(uint8_t pc_sse_id);

/** @brief The id a route carries when it serves no SSE stream. */
#define PC_SSE_NONE 0xFFu

/**
 * @brief Record one route's subscribe handler and return the id naming it, or ::PC_SSE_NONE when full.
 *
 * The handler lives here, not in the route table: a route decides where a request goes, and what
 * runs once a client subscribes belongs to this module.
 */
uint8_t pc_sse_route_add(SseConnectHandler on_connect);

/// @brief The subscribe handler @p id names, or nullptr when @p id names nothing.
SseConnectHandler pc_sse_route_connect(uint8_t id);

/**
 * @brief Initialize all SSE pool slots to inactive.
 *
 * Called once from begin().
 */
void pc_sse_init();

/**
 * @brief Allocate an SseConn and bind it to a TCP slot.
 *
 * @param slot_id  TCP slot that just received the SSE subscription request.
 * @param path     URL path the client subscribed to (stored for broadcast).
 * @return Pointer to the allocated SseConn, or nullptr if the pool is full.
 */
SseConn *pc_sse_alloc(uint8_t slot_id, const char *path);

/**
 * @brief Find the SseConn for a given TCP slot, or nullptr.
 *
 * @param slot_id  TCP connection slot index.
 */
SseConn *pc_sse_find(uint8_t slot_id);

/**
 * @brief Free the SseConn associated with a TCP slot.
 *
 * @param slot_id  TCP connection slot index.
 */
void pc_sse_free(uint8_t slot_id);

/**
 * @brief Format one SSE event record into a caller buffer (no transport).
 *
 * Emits `event: <event>\n` (if event), `id: <id>\n` (if id), then
 * `data: <data>\n\n` per the WHATWG event-stream format.  data must not be
 * nullptr.  Pure: no connection state, so it is unit-testable and benchable
 * on its own; pc_sse_write() wraps it with the Tcp.conn->send() I/O.
 *
 * @param buf    Destination buffer.
 * @param n      Size of @p buf.
 * @param data   Event data (required).
 * @param event  Event name (optional).
 * @param id     Event ID (optional).
 * @return Bytes written (excluding the terminator), or 0 on empty/overflow.
 */
int pc_sse_format(char *buf, size_t n, const char *data, const char *event, const char *id);

/**
 * @brief Write one SSE event record to a client.
 *
 * Formats and sends `event: ...\nid: ...\ndata: ...\n\n`.  Any optional
 * field may be nullptr to omit it.  data must not be nullptr.
 *
 * The caller must flush the connection afterwards (Tcp.conn->flush()) if
 * immediate delivery is needed.
 *
 * @param sse    SSE connection.
 * @param data   Event data (required).
 * @param event  Event name (optional).
 * @param id     Event ID (optional).
 * @return true on success, false if the TCP slot is not active.
 */
proto_bool pc_sse_write(SseConn *sse, const char *data, const char *event, const char *id);

PROTO_END_DECLS

#endif
