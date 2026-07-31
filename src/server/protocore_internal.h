// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_internal.h
 * @brief Library-private declarations shared between protocore.cpp and the src/server/*.cpp
 *        request-handler translation units it is split into (WebDAV, file serving, ...).
 *
 * These are NOT part of the public API - they are the handful of protocore.cpp file-scope helpers
 * that a split-out handler still needs, promoted from `static` to external linkage so the pieces
 * link together. Everything else a handler needs is either a public PC method (declared
 * in protocore.h) or already an extern in the transport headers (e.g. conn_pool in tcp.h).
 */

#ifndef PROTOCORE_INTERNAL_H
#define PROTOCORE_INTERNAL_H

#include "protocore.h"
#include <time.h>

/** @brief Reason phrase for an HTTP status code (e.g. 404 -> "Not Found"). */
const char *status_text(int code);

/**
 * @brief The fixed reply sent when a response's own headers will not fit RESP_HDR_BUF_SIZE.
 *
 * A header block cut short has no terminating CRLF, so the peer keeps reading the body as headers
 * and the connection desynchronizes - which is why truncating is not an option and this always-
 * fitting reply goes out instead. Connection: close, because the request is not recoverable.
 */
extern const char PC_RESP_HDR_OVERFLOW[];

/** @brief Length of PC_RESP_HDR_OVERFLOW, taken with sizeof where the array bound is still visible. */
extern const size_t PC_RESP_HDR_OVERFLOW_LEN;

/** @brief Initialize the common fields (path, flags) of a route-table entry from its pattern. */
void fill_route_base(Route *r, const char *path);

/** @brief Format @p t as an RFC 1123 GMT date into @p out (cap bytes); @p out is emptied for t <= 0. */
void http_rfc1123(time_t t, char *out, size_t cap);

/** @brief True if the request in slot @p slot_id used the HEAD method (send headers, no body). */
bool req_is_head(uint8_t slot_id);

/** @brief Whole-path regex match (anchored both ends; bounded by RE_MAX_STEPS, fails closed).
 *  Defined in server/regex.cpp, called by the route dispatcher for `on_regex()` routes. */
bool regex_match(const char *pattern, const char *path);

// ---------------------------------------------------------------------------
// Outbound-transfer continuations (owned by protocore.cpp, shared with the split handlers)
// ---------------------------------------------------------------------------

// A transfer in flight owns its slot: the poll skips the rest of the pipeline until the body is
// out. Each kind of transfer is owned by the TU that runs it - the chunked-send state by
// server/response.cpp, the file-send state by server/file_serving.cpp - so neither struct is
// declared here and neither is reachable from anywhere but its owner. The poll asks each owner
// whether it holds the slot rather than reading its state.

/** @brief True while a chunked response is paging out on @p slot (owner: server/response.cpp). */
bool pc_resp_holds_slot(uint8_t slot);

#if PC_ENABLE_FILE_SERVING
/** @brief True while a file response is paging out on @p slot (owner: server/file_serving.cpp). */
bool pc_file_holds_slot(uint8_t slot);
#endif

// ---------------------------------------------------------------------------
// WebSocket / SSE upgrade entry points (defined in server/websocket_sse.cpp, called
// by the route dispatcher in protocore.cpp when a matched route is a WS/SSE endpoint).
// ---------------------------------------------------------------------------

#if PC_ENABLE_WEBSOCKET
/** @brief Perform the RFC 6455 101 handshake and hand the slot to the WS frame parser. */
bool ws_do_upgrade(uint8_t slot_id, HttpReq *req, WsConnectHandler on_connect);

/** @brief Reject an unsupported Sec-WebSocket-Version with a 426 (RFC 6455 4.2.1) and close. */
void ws_send_version_required(uint8_t slot_id);
#endif

#if PC_ENABLE_SSE
/** @brief Send the SSE 200 headers and promote the slot to server-sent-events mode. */
bool pc_sse_do_upgrade(uint8_t slot_id, HttpReq *req, SseConnectHandler on_connect);
#endif

#endif // PROTOCORE_INTERNAL_H
