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

#include "../transport/tcp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <Arduino.h>

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
 *    ParseState::PARSE_COMPLETE slots and send responses.
 *
 * @note The event-drain loop is bounded by the queue depth (16 entries).
 *       Even in the absolute worst case this function executes in O(1).
 */
void server_tick(int worker_id = 0);

// ---------------------------------------------------------------------------
// Forward declarations for Layer 6 functions used by server_tick()
// ---------------------------------------------------------------------------

/*
 * Reset a presentation-layer parser slot (Layer 6 forward decl).
 * @param slot_id Connection slot to reset.
 * @see http_reset in presentation.h
 */
void http_reset(uint8_t slot_id);

/*
 * Initialize a slot for a freshly-accepted HTTP connection (Layer 6 forward decl).
 * Resets the parser and (keep-alive) the per-connection request tally.
 * @param slot_id Connection slot to open.
 * @see http_conn_open in presentation.h
 */
void http_conn_open(uint8_t slot_id);

/*
 * Advance the HTTP parser for a slot (Layer 6 forward decl).
 * @param slot_id Connection slot to parse.
 * @see http_parse in presentation.h
 */
void http_parse(uint8_t slot_id);

#endif
