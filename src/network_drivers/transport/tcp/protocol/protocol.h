// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocol.h
 * @brief Layer 4 (Transport) - the user/TCP interface and the event processing behind it.
 *
 * RFC 9293 sec 3.9.1 names the calls a TCP implementation owes its user: Open, Send, Receive,
 * Close, Status, Abort, Flush, Asynchronous Reports, Set Differentiated Services Field. ::ConnPoolNs
 * is that set for an accepted connection - the pool holds what the server accepted, and every layer
 * above reaches a connection through this table rather than through the stack.
 *
 * Behind the calls is sec 3.10 Event Processing: the three stack callbacks (sec 3.10.7 SEGMENT
 * ARRIVES), the close sequence and its drain dwell (sec 3.6), the window management that reopens on
 * consume (sec 3.8.6), and the timeouts (sec 3.10.8).
 *
 * Calls INTO the stack are not here - those are lower.h (sec 3.9.2). This module decides what to
 * do; that one performs it in the context where it is safe.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_PROTOCOL_H
#define PROTOCORE_TCP_PROTOCOL_H

#include "../evt.h"                                       // ConnState, TcpEvt, and the observability hook
#include "core_setup/board_profiles/protocore_platform.h" // protocore_pcb, protocore_net_err: the types a call names
#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Event processing - the stack's callbacks (RFC 9293 sec 3.10.7 SEGMENT ARRIVES)
// ---------------------------------------------------------------------------
// The stack calls these directly, so their shapes are its and not this module's. Non-static so the
// server's accept path can take their address, and so a host test can drive them: on native there
// is no real stack event to fire them.

protocore_net_err lowlevel_recv_cb(void *arg, protocore_pcb *tpcb, protocore_pbuf *p, protocore_net_err err);
protocore_net_err lowlevel_sent_cb(void *arg, protocore_pcb *tpcb, proto_u16 len);
void lowlevel_err_cb(void *arg, protocore_net_err err);

/** @brief RFC 9293 sec 3.9.1 SEND / RECEIVE: the bytes a call moves, and the room they move into. */
typedef struct
{
    const void *data; ///< bytes for a send
    proto_u16 len;    ///< how many
    uint8_t *buf;     ///< where a read or a peek lands
    size_t cap;       ///< how much room it has
    size_t off;       ///< a peek's offset from the tail
    size_t count;     ///< bytes a peek copies, or a consume drops
} ConnIoArgs;

/** @brief What a pool lifecycle call reads: the config it comes up from, and whose slots it sweeps. */
typedef struct
{
    const WebServerConfig *cfg; ///< the config init reads
    int worker_id;              ///< whose slots the sweep reaps
} ConnLifeArgs;

#if PROTOCORE_ENABLE_OBSERVABILITY
/** @brief What an observability call records; nothing on the byte path reads these. */
typedef struct
{
    protocore_conn_event_cb event_cb_in; ///< the observer on_event installs
    protocore_conn_reason reason;        ///< why a transition or notice fired
    ConnState olds;                      ///< the state a transition left
    ConnState news;                      ///< the state it entered
    protocore_conn_counters counters;    ///< where counters_get reports
} ConnObsArgs;
#endif

/** @brief The pool's own state and the calls that reach it, described only in protocol.c. */
struct ConnPoolInternal;

/**
 * @brief The pool of accepted connections: the user/TCP interface for one connection.
 *
 * Named for the pool rather than the slot: a slot is named by its index, and its layout is the
 * transport's own.
 *
 * A caller sets the members a call takes, invokes it through ::ConnPool, and reads the result off
 * the same handle. The pool's own state - the idle bound, the counters, the template a slot is
 * reset from, and the calls themselves - is behind @ref internal and is not describable here.
 *
 * @var ConnPoolNs::slot       the connection a call acts on
 * @var ConnPoolNs::st         the state a write installs
 * @var ConnPoolNs::data       bytes for a send
 * @var ConnPoolNs::len        how many
 * @var ConnPoolNs::pcb        the control block a raw call acts on
 * @var ConnPoolNs::cfg        the config init reads
 * @var ConnPoolNs::worker_id  whose slots the sweep reaps
 * @var ConnPoolNs::out        where the peer address is written
 * @var ConnPoolNs::evt        the event an enqueue posts
 * @var ConnPoolNs::buf        where a read or a peek lands
 * @var ConnPoolNs::cap        how much room it has
 * @var ConnPoolNs::off        a peek's offset from the tail
 * @var ConnPoolNs::count      bytes a peek copies, or a consume drops
 * @var ConnPoolNs::ok         a call's true/false outcome
 * @var ConnPoolNs::u16        a call's 16-bit outcome
 * @var ConnPoolNs::u32        a call's 32-bit outcome
 * @var ConnPoolNs::u8         a call's 8-bit outcome
 * @var ConnPoolNs::i32        a call's signed outcome
 * @var ConnPoolNs::n          a byte count a call reports
 * @var ConnPoolNs::if_kind    the interface a slot's connection arrived on
 * @var ConnPoolNs::proto      the application protocol a slot carries
 * @var ConnPoolNs::set_state         install the state in st on the slot
 * @var ConnPoolNs::alloc_free        claim the lowest free slot
 * @var ConnPoolNs::timeout_ms        the idle deadline the sweep measures against
 * @var ConnPoolNs::send              queue bytes for transmission (RFC 9293 sec 3.9.1 SEND)
 * @var ConnPoolNs::send_flush        the same, pushed on the way out
 * @var ConnPoolNs::sndbuf            room the send buffer has left
 * @var ConnPoolNs::flush             push what is queued (RFC 9293 sec 3.9.1 PUSH)
 * @var ConnPoolNs::ack_consumed      reopen the receive window by what was drained
 * @var ConnPoolNs::raw_send          write already-encrypted bytes, no TLS re-entry
 * @var ConnPoolNs::close             tear the connection down (RFC 9293 sec 3.9.1 CLOSE)
 * @var ConnPoolNs::abort_slot        reset it (RFC 9293 sec 3.9.1 ABORT)
 * @var ConnPoolNs::closing_finalize  finish a slot whose TX has drained
 * @var ConnPoolNs::closing_check     finalize it if it has
 * @var ConnPoolNs::begin_close       enter the dwell that precedes the close
 * @var ConnPoolNs::enqueue           post an event to the owning listener's queue
 * @var ConnPoolNs::init              bring the pool up from cfg
 * @var ConnPoolNs::stop              take every slot down
 * @var ConnPoolNs::active_count      how many slots are live
 * @var ConnPoolNs::remote_ip         the peer address of a slot
 * @var ConnPoolNs::remote_addr       the same, formatted
 * @var ConnPoolNs::touch_active      restart a slot's idle timer
 * @var ConnPoolNs::check_timeouts    reap the slots whose deadline passed
 * @var ConnPoolNs::on_event          install the observer in event_cb_in
 * @var ConnPoolNs::counters_get      read the counters out
 * @var ConnPoolNs::counters_reset    zero them
 * @var ConnPoolNs::obs_bump          count one reason
 * @var ConnPoolNs::obs_transition    record a state change
 * @var ConnPoolNs::obs_notice        record a notice against the current state
 * @var ConnPoolNs::available         bytes the slot's receive ring holds
 * @var ConnPoolNs::read_byte         pop one byte into u8
 * @var ConnPoolNs::peek              copy count bytes at off into buf without consuming
 * @var ConnPoolNs::consume           drop count bytes from the tail
 * @var ConnPoolNs::read              pop up to cap bytes into buf
 * @var ConnPoolNs::active            the slot holds a live connection that can take a send
 * @var ConnPoolNs::iface             the interface it arrived on
 * @var ConnPoolNs::listener_id       the listener it was accepted on
 * @var ConnPoolNs::tls               the connection began a TLS handshake
 * @var ConnPoolNs::owner             the worker that owns the slot
 * @var ConnPoolNs::proto_of          the application protocol it carries
 * @var ConnPoolNs::pcb_of            its control block
 * @var ConnPoolNs::internal   the pool's state and the calls that reach it
 */
typedef struct
{
    uint8_t slot;       ///< the connection every call names
    ConnState st;       ///< the state a write installs
    protocore_pcb *pcb; ///< the control block a raw call acts on, or the one pcb_of reports
    const TcpEvt *evt;  ///< the event an enqueue posts

    ConnIoArgs io;     ///< the bytes a send or a receive moves (RFC 9293 sec 3.9.1)
    ConnLifeArgs life; ///< what a pool lifecycle call reads
#if PROTOCORE_ENABLE_OBSERVABILITY
    ConnObsArgs obs; ///< what an observability call records
#endif

    proto_bool ok;
    proto_u16 u16;
    uint32_t u32;
    uint8_t u8;
    int32_t i32;
    size_t n;
    protocore_if_kind if_kind;
    ProtoConn proto;
    protocore_ip *out; ///< where a peer address is written

    void (*set_state)(struct ConnPoolInternal *ctx);
    void (*alloc_free)(struct ConnPoolInternal *ctx);
    void (*timeout_ms)(struct ConnPoolInternal *ctx);
    void (*send)(struct ConnPoolInternal *ctx);
    void (*send_flush)(struct ConnPoolInternal *ctx);
    void (*sndbuf)(struct ConnPoolInternal *ctx);
    void (*flush)(struct ConnPoolInternal *ctx);
    void (*ack_consumed)(struct ConnPoolInternal *ctx);
    void (*raw_send)(struct ConnPoolInternal *ctx);
    void (*close)(struct ConnPoolInternal *ctx);
    void (*abort_slot)(struct ConnPoolInternal *ctx);
    void (*closing_finalize)(struct ConnPoolInternal *ctx);
    void (*closing_check)(struct ConnPoolInternal *ctx);
    void (*begin_close)(struct ConnPoolInternal *ctx);
    void (*enqueue)(struct ConnPoolInternal *ctx);
    void (*init)(struct ConnPoolInternal *ctx);
    void (*stop)(struct ConnPoolInternal *ctx);
    void (*active_count)(struct ConnPoolInternal *ctx);
    void (*remote_ip)(struct ConnPoolInternal *ctx);
    void (*remote_addr)(struct ConnPoolInternal *ctx);
    void (*touch_active)(struct ConnPoolInternal *ctx);
    void (*check_timeouts)(struct ConnPoolInternal *ctx);
#if PROTOCORE_ENABLE_OBSERVABILITY
    void (*on_event)(struct ConnPoolInternal *ctx);
    void (*counters_get)(struct ConnPoolInternal *ctx);
    void (*counters_reset)(struct ConnPoolInternal *ctx);
    void (*obs_bump)(struct ConnPoolInternal *ctx);
    void (*obs_transition)(struct ConnPoolInternal *ctx);
    void (*obs_notice)(struct ConnPoolInternal *ctx);
#endif

    // The receive ring, and what a slot is. Transport owns the ring: a layer above drains it only
    // through these, and never indexes the buffer or advances the tail itself.
    void (*available)(struct ConnPoolInternal *ctx);
    void (*read_byte)(struct ConnPoolInternal *ctx);
    void (*peek)(struct ConnPoolInternal *ctx);
    void (*consume)(struct ConnPoolInternal *ctx);
    void (*read)(struct ConnPoolInternal *ctx);
    void (*active)(struct ConnPoolInternal *ctx);
    void (*iface)(struct ConnPoolInternal *ctx);
    void (*listener_id)(struct ConnPoolInternal *ctx);
    void (*tls)(struct ConnPoolInternal *ctx);
    void (*owner)(struct ConnPoolInternal *ctx);
    void (*proto_of)(struct ConnPoolInternal *ctx);
    void (*pcb_of)(struct ConnPoolInternal *ctx);

    struct ConnPoolInternal *internal;
} ConnPoolNs;

/** @brief The one symbol this module exports. */
extern ConnPoolNs ConnPool;

PROTOCORE_END_DECLS

#endif // PROTOCORE_TCP_PROTOCOL_H
