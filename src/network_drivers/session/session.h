// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file session.h
 * @brief Layer 5 (Session) - event queue dispatcher and session lifecycle.
 *
 * The session layer is the bridge between the interrupt-driven transport
 * layer and the application-layer HTTP handler.  It processes all pending
 * events from the FreeRTOS queue in a single bounded loop, ensuring that
 * `server_tick()` has a deterministic worst-case execution time of
 * O(queue_depth + MAX_CONNS).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SESSION_H
#define PROTOCORE_SESSION_H

#include "../transport/tcp_evt.h" // EvtType, TcpEvt: the events this layer drains

/**
 * @brief Drive the session layer for one Arduino loop iteration.
 *
 * Call this function from your `loop()` (or indirectly via
 * handle()).  It performs three actions in order:
 *
 * 1. **Timeout sweep** - calls DeterministicAsyncTCP::check_timeouts()
 *    to force-close connections that have been idle for > CONN_TIMEOUT_MS.
 *
 * 2. **Event drain** - dequeues all pending TcpEvt records from the
 *    FreeRTOS queue.  Each event is dispatched:
 *    - `EvtType::EVT_CONNECT / EvtType::EVT_DISCONNECT / EvtType::EVT_ERROR` → http_reset()
 *    - `EvtType::EVT_DATA` → http_parse()
 *
 * 3. **Returns** - upper layers may then inspect http_pool[] for
 *    PARSE_COMPLETE slots and send responses.
 *
 * @note The event-drain loop is bounded by the queue depth (16 entries).
 *       Even in the absolute worst case this function executes in O(1).
 */
void server_tick(int worker_id);

#endif
