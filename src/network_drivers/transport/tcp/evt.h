// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file evt.h
 * @brief What the stack callbacks post to a listener's queue: the event type and the record.
 *
 * Separate from common.h because this is all the layers above the transport need. A listener sizes
 * its queue storage on ::TcpEvt, the session layer switches on ::EvtType, and the presentation
 * layer names EVT_CONNECT; none of them touches a connection slot's fields. common.h carries those,
 * and the ring cursors in them are `_Atomic`, which is C11 and not C++ - so a header that reaches
 * the sketches through protocore.h cannot be the one that declares them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_EVT_H
#define PROTOCORE_TCP_EVT_H

#include "protocore_config.h" // the entry point: the widths, PROTO_ENUM_PACKED, static_assert

/**
 * @brief Lifecycle state of a connection pool slot.
 *
 * NOT the RFC 9293 sec 3.3.2 connection state machine. That machine (LISTEN, SYN-RECEIVED,
 * ESTABLISHED, FIN-WAIT-1/2, CLOSE-WAIT, CLOSING, LAST-ACK, TIME-WAIT) belongs to the stack under
 * this layer; these three name only whether the pool slot is available. A test mapping CONN_* to
 * RFC state names 1:1 is testing the wrong thing.
 *
 * Transitions, as the code performs them:
 * - `CONN_FREE -> CONN_ACTIVE`     accept callback fires.
 * - `CONN_ACTIVE -> CONN_CLOSING`  the dwell that precedes a close begins.
 * - `CONN_CLOSING -> CONN_FREE`    the peer ACKed the outbound data (snd_queuelen == 0), the dwell
 *                                  timed out, or data arrived that could no longer be delivered.
 * - `CONN_ACTIVE -> CONN_FREE`     local close, remote FIN, stack error, or the idle sweep.
 *
 * Every terminal edge detaches the control block and frees the slot before handing it to the stack,
 * so the slot's lifetime ends before the connection's does; the FIN and its retransmission are the
 * stack's from that point (RFC 9293 sec 3.6).
 *
 * Here rather than in common.h because the signaling layer reads a slot's state and reaches this
 * layer through protocore.h; the enum is one packed byte and carries no `_Atomic`.
 */
typedef enum PROTO_ENUM_PACKED
{
    CONN_FREE,   ///< Slot is available; no control block is attached.
    CONN_ACTIVE, ///< Live connection; the control block is valid.
    CONN_CLOSING ///< Transmit-drain dwell: holding the slot until the peer ACKs what was sent. No
                 ///< FIN has been emitted yet - it goes out as the slot is released.
} ConnState;
static_assert(sizeof(ConnState) == 1,
              "ConnState must stay one byte (PROTO_ENUM_PACKED); TcpConn and conn_pool[] size themselves on it");

/**
 * @brief Type of connection event posted to a listener's event queue.
 *
 * EVT_DISCONNECT and EVT_ERROR are the two a close is reported through, and they are distinct
 * because RFC 9293 sec 3.6 MUST-12 requires the layer above to be told whether a connection closed
 * normally or was aborted.
 */
typedef enum PROTO_ENUM_PACKED
{
    EVT_CONNECT,    ///< New connection accepted.
    EVT_DATA,       ///< Data received; bytes are already in the ring buffer.
    EVT_DISCONNECT, ///< Remote peer closed the connection gracefully.
    EVT_ERROR       ///< The stack reported an error (the control block may already be freed).
} EvtType;
static_assert(sizeof(EvtType) == 1,
              "EvtType must stay one byte (PROTO_ENUM_PACKED); every listener's queue storage sizes itself on TcpEvt");

/**
 * @brief Event record posted from the stack callbacks to the session layer.
 *
 * Copied into the queue by value, so nothing in it points at a slot whose lifetime ends before the
 * record is drained.
 */
typedef struct TcpEvt
{
    EvtType type;    ///< What happened.
    uint8_t slot_id; ///< Which connection slot is affected.
    size_t data_len; ///< Bytes copied (EVT_DATA only); 0 for other types.
} TcpEvt;

// ---------------------------------------------------------------------------
// Observability (PROTOCORE_ENABLE_OBSERVABILITY) - connection event hook + counters
// ---------------------------------------------------------------------------
#if PROTOCORE_ENABLE_OBSERVABILITY

/** @brief Why a connection event fired (the reason for a transition or notice). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_CONN_R_ACCEPT,       ///< New connection accepted (CONN_FREE -> CONN_ACTIVE).
    PROTOCORE_CONN_R_CLOSE_REMOTE, ///< Peer closed gracefully (FIN received).
    PROTOCORE_CONN_R_CLOSE_LOCAL,  ///< Application initiated the close.
    PROTOCORE_CONN_R_ERROR,        ///< The stack reported a fatal error on the connection.
    PROTOCORE_CONN_R_TIMEOUT,      ///< Idle-timeout sweep reaped the slot.
    PROTOCORE_CONN_R_ABORT,        ///< Forced abort (server stop / pool reset / data after close).
    PROTOCORE_CONN_R_DRAINED,      ///< CONN_CLOSING slot finished draining -> closed.
    PROTOCORE_CONN_R_BACKPRESSURE, ///< RX segment refused (ring full); no state change.
    PROTOCORE_CONN_R_DEFER_DROP    ///< Event queue full; an event was dropped (no state change).
} protocore_conn_reason;
static_assert(sizeof(protocore_conn_reason) == 1, "protocore_conn_reason must stay one byte (PROTO_ENUM_PACKED)");

/** @brief Snapshot of the transport's lifetime counters (plus a live gauge). */
typedef struct protocore_conn_counters
{
    uint32_t accepts;        ///< Connections accepted.
    uint32_t closes_remote;  ///< Closed by peer FIN.
    uint32_t closes_local;   ///< Closed by the application.
    uint32_t closes_error;   ///< Closed by a stack error.
    uint32_t closes_timeout; ///< Reaped by the idle-timeout sweep.
    uint32_t closes_abort;   ///< Force-aborted (stop / reset / data after close).
    uint32_t backpressure;   ///< RX segments refused for lack of ring space.
    uint32_t defer_drops;    ///< Deferred events dropped because the queue was full.
    uint32_t closing_gauge;  ///< Slots currently in CONN_CLOSING (live, not cumulative).
} protocore_conn_counters;

/**
 * @brief Callback fired on every connection state transition.
 *
 * Runs in whichever task drove the transition (the stack's callback context for
 * accept / recv / error, a worker for close / timeout), so keep it short and non-blocking and do
 * not call back into the server from it. @p old_state == @p new_state for the
 * non-transition notices (backpressure, defer-drop).
 */
typedef void (*protocore_conn_event_cb)(uint8_t slot, ConnState old_state, ConnState new_state,
                                        protocore_conn_reason reason);

// Internal notify points (protocol/protocol.c), reached via the macros below so the protocol
// engine and the server's accept path both record through one path.
void protocore_obs_transition(uint8_t slot, ConnState olds, ConnState news, protocore_conn_reason reason);
void protocore_obs_notice(uint8_t slot, ConnState st, protocore_conn_reason reason);
#define PROTOCORE_OBS_TRANSITION(slot, olds, news, reason) protocore_obs_transition((slot), (olds), (news), (reason))
#define PROTOCORE_OBS_NOTICE(slot, st, reason) protocore_obs_notice((slot), (st), (reason))

#else // !PROTOCORE_ENABLE_OBSERVABILITY

// Compile to nothing; the arguments (incl. protocore_conn_reason names, only declared
// when the feature is on) are dropped unparsed by the preprocessor.
#define PROTOCORE_OBS_TRANSITION(slot, olds, news, reason) ((void)0)
#define PROTOCORE_OBS_NOTICE(slot, st, reason) ((void)0)

#endif // PROTOCORE_ENABLE_OBSERVABILITY

#endif // PROTOCORE_TCP_EVT_H
