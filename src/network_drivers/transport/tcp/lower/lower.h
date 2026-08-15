// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lower.h
 * @brief Layer 4 (Transport) - the TCP/lower-level interface (RFC 9293 sec 3.9.2).
 *
 * "The TCP endpoint calls on a lower-level protocol module to actually send and receive
 * information over a network." Every such call this library makes goes through here: the write,
 * the push, the close, the reset, the window update, and the DS field (sec 3.9.2 names the
 * Diffserv value as one the user supplies to the lower layer).
 *
 * The byte stream is the floor. Nothing below the platform's own TCP is modelled here; this module
 * is the one place that names it, so no other file in the layer holds a raw stack call.
 *
 * **Why the ops are marshaled**
 * The raw stack API is not thread-safe: its callbacks run in the stack's own task, while this
 * library issues writes and closes from a worker. Issuing one concurrently with the stack
 * processing an inbound segment corrupts the connection state. The portable fix is the stack's own
 * marshaling call, which runs a function inside the stack's context and blocks the caller until it
 * completes. A raw callback already runs in that context and must NOT marshal again - it performs
 * its op inline instead, or it would block on the very mailbox its own thread services.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_LOWER_H
#define PROTOCORE_TCP_LOWER_H

#include "../common.h"
#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief The TTL outbound TCP segments carry.
 *
 * RFC 9293 sec 3.9.2 MUST-49: "The TTL value used to send TCP segments MUST be configurable." RFC
 * 793 fixed it at one minute; RFC 1122 replaced that with this requirement. Overridable at build
 * time; the running value is set through protocore_tcp_set_ttl().
 */
#ifndef PROTOCORE_TCP_TTL
#define PROTOCORE_TCP_TTL 64
#endif

/** @brief Which call into the lower-level module a marshaled op performs. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_OP_SEND,
    PROTOCORE_OP_OUTPUT,
    PROTOCORE_OP_CLOSE,
    PROTOCORE_OP_ABORT,
    PROTOCORE_OP_DETACH,
    PROTOCORE_OP_RAWSEND,     // raw write of already-encrypted bytes (TLS BIO), no TLS re-entry
    PROTOCORE_OP_CLOSE_CHECK, // in stack context: finalize a CONN_CLOSING slot if its TX has drained
    PROTOCORE_OP_RECVED,      // in stack context: reopen the receive window (ack-on-consume)
    PROTOCORE_OP_SET_TTL,     // in stack context: stamp the TTL on a control block; len carries the value
} protocore_tcp_op;
static_assert(sizeof(protocore_tcp_op) == 1, "protocore_tcp_op must stay one byte (PROTO_ENUM_PACKED)");

/** @brief The seam's own state, held where only lower.c describes it. */
struct TcpLowerInternal;

/**
 * @brief The lower-level interface: what this endpoint can ask the module below it to do.
 *
 * A caller sets the op it wants and what it acts on, invokes it through ::TcpLower, and reads the
 * outcome off the same handle. The seam's own state - the stack thread it captured, the TTL it
 * stamps, and the call records - is behind @ref internal and is not describable here.
 *
 * @var TcpLowerNs::op        which call into the lower module to make
 * @var TcpLowerNs::slot      the connection the op acts on
 * @var TcpLowerNs::pcb       the control block the op acts on
 * @var TcpLowerNs::data      bytes for a write
 * @var TcpLowerNs::len       how many, or the byte a stamping op carries
 * @var TcpLowerNs::flush     SEND: push after a successful write
 * @var TcpLowerNs::result    what the op reported
 * @var TcpLowerNs::internal  the seam's state and the calls that reach it
 */
typedef struct
{
    protocore_tcp_op op;
    uint8_t slot;
    protocore_pcb *pcb;
    const void *data;
    proto_u16 len;
    proto_bool flush;
    protocore_net_err result;

    /// Run the op set above, in the one context where it is safe.
    protocore_net_err (*marshal)(struct TcpLowerInternal *ctx);
    /// Drop the control block's back-reference, so a late callback finds a null arg.
    void (*detach)(struct TcpLowerInternal *ctx);
    /// Reset the control block (RFC 9293 sec 3.10.5): a hard close, no FIN.
    void (*abort)(struct TcpLowerInternal *ctx);
    /// Install the TTL outbound segments carry; the candidate arrives in len.
    proto_bool (*set_ttl)(struct TcpLowerInternal *ctx);
    /// Stamp the control block above with the configured TTL.
    void (*apply_ttl)(struct TcpLowerInternal *ctx);

    struct TcpLowerInternal *internal;
} TcpLowerNs;

/** @brief The one symbol this module exports. */
extern TcpLowerNs TcpLower;

PROTOCORE_END_DECLS

#endif // PROTOCORE_TCP_LOWER_H
