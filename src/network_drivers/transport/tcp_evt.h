// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tcp_evt.h
 * @brief What the stack callbacks post to a listener's queue: the event type and the record.
 *
 * Separate from tcp.h because this is all the layers above the transport need. A listener sizes
 * its queue storage on ::TcpEvt, the session layer switches on ::EvtType, and the presentation
 * layer names EVT_CONNECT; none of them touches a connection slot's fields. tcp.h carries those,
 * and the ring cursors in them are `_Atomic`, which is C11 and not C++ - so a header that reaches
 * the sketches cannot be the one that declares them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_EVT_H
#define PROTOCORE_TCP_EVT_H

#include "protocore_config.h" // the entry point: the widths, PROTO_ENUM_PACKED, static_assert

/**
 * @brief Type of connection event posted to a listener's event queue.
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
 * Small enough (≤12 bytes on 32-bit) that the queue copies it by
 * value - no pointer lifetime issues.
 */
typedef struct TcpEvt
{
    EvtType type;    ///< What happened.
    uint8_t slot_id; ///< Which connection slot is affected.
    size_t data_len; ///< Bytes copied (EVT_DATA only); 0 for other types.
} TcpEvt;

#endif // PROTOCORE_TCP_EVT_H
