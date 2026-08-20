// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "../evt.h"                   // ConnState, TcpEvt, and the observability hook
#include "config/platform/platform.h" // protocore_pcb, protocore_net_err: the types a call names
#include "shared/ip/ip.h"             // protocore_ip: where a peer address is written

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
/** @brief What a pool lifecycle call reads: the idle deadline it sweeps against, and whose slots. */
typedef struct
{
    proto_u32 conn_timeout_ms; ///< milliseconds of inactivity before the sweep closes a slot
    int worker_id;             ///< whose slots the sweep reaps
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
 * @var ConnPoolNs::conn_timeout_ms  the idle deadline init loads
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
 * @var ConnPoolNs::init              bring the pool up on the idle deadline it was given
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
 */
typedef struct
{
    uint8_t slot;       ///< the connection every call names
    ConnState st;       ///< the state a write installs
    protocore_pcb *pcb; ///< the control block a raw call acts on, or the one pcb_of reports
    const TcpEvt *evt;  ///< the event an enqueue posts
    ConnIoArgs io;      ///< the bytes a send or a receive moves (RFC 9293 sec 3.9.1)
    ConnLifeArgs life;  ///< what a pool lifecycle call reads
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
#if PROTOCORE_ENABLE_OBSERVABILITY
#endif
    // The receive ring, and what a slot is. Transport owns the ring: a layer above drains it only
    // through these, and never indexes the buffer or advances the tail itself.
} ConnPoolVars;

/** @brief The operands and the outcome. */
extern ConnPoolVars ConnPoolV;

/** @brief The entries. */
typedef struct
{
    void (*const set_state)(uint8_t *restrict work);
    void (*const alloc_free)(uint8_t *restrict work);
    void (*const timeout_ms)(uint8_t *restrict work);
    void (*const send)(uint8_t *restrict work);
    void (*const send_flush)(uint8_t *restrict work);
    void (*const sndbuf)(uint8_t *restrict work);
    void (*const flush)(uint8_t *restrict work);
    void (*const ack_consumed)(uint8_t *restrict work);
    void (*const raw_send)(uint8_t *restrict work);
    void (*const close)(uint8_t *restrict work);
    void (*const abort_slot)(uint8_t *restrict work);
    void (*const closing_finalize)(uint8_t *restrict work);
    void (*const closing_check)(uint8_t *restrict work);
    void (*const begin_close)(uint8_t *restrict work);
    void (*const enqueue)(uint8_t *restrict work);
    void (*const init)(uint8_t *restrict work);
    void (*const stop)(uint8_t *restrict work);
    void (*const active_count)(uint8_t *restrict work);
    void (*const remote_ip)(uint8_t *restrict work);
    void (*const remote_addr)(uint8_t *restrict work);
    void (*const touch_active)(uint8_t *restrict work);
    void (*const check_timeouts)(uint8_t *restrict work);
    void (*const on_event)(uint8_t *restrict work);
    void (*const counters_get)(uint8_t *restrict work);
    void (*const counters_reset)(uint8_t *restrict work);
    void (*const obs_bump)(uint8_t *restrict work);
    void (*const obs_transition)(uint8_t *restrict work);
    void (*const obs_notice)(uint8_t *restrict work);
    void (*const available)(uint8_t *restrict work);
    void (*const read_byte)(uint8_t *restrict work);
    void (*const peek)(uint8_t *restrict work);
    void (*const consume)(uint8_t *restrict work);
    void (*const read)(uint8_t *restrict work);
    void (*const active)(uint8_t *restrict work);
    void (*const iface)(uint8_t *restrict work);
    void (*const listener_id)(uint8_t *restrict work);
    void (*const tls)(uint8_t *restrict work);
    void (*const owner)(uint8_t *restrict work);
    void (*const proto_of)(uint8_t *restrict work);
    void (*const pcb_of)(uint8_t *restrict work);
} ConnPoolNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in ConnPoolV or a region of the borrow at a fixed offset.
void protocore_protocol_set_state(uint8_t *restrict work);
void protocore_protocol_alloc_free(uint8_t *restrict work);
void protocore_protocol_timeout_ms(uint8_t *restrict work);
void protocore_protocol_send(uint8_t *restrict work);
void protocore_protocol_send_flush(uint8_t *restrict work);
void protocore_protocol_sndbuf(uint8_t *restrict work);
void protocore_protocol_flush(uint8_t *restrict work);
void protocore_protocol_ack_consumed(uint8_t *restrict work);
void protocore_protocol_raw_send(uint8_t *restrict work);
void protocore_protocol_close(uint8_t *restrict work);
void protocore_protocol_abort_slot(uint8_t *restrict work);
void protocore_protocol_closing_finalize(uint8_t *restrict work);
void protocore_protocol_closing_check(uint8_t *restrict work);
void protocore_protocol_begin_close(uint8_t *restrict work);
void protocore_protocol_enqueue(uint8_t *restrict work);
void protocore_protocol_init(uint8_t *restrict work);
void protocore_protocol_stop(uint8_t *restrict work);
void protocore_protocol_active_count(uint8_t *restrict work);
void protocore_protocol_remote_ip(uint8_t *restrict work);
void protocore_protocol_remote_addr(uint8_t *restrict work);
void protocore_protocol_touch_active(uint8_t *restrict work);
void protocore_protocol_check_timeouts(uint8_t *restrict work);
void protocore_protocol_on_event(uint8_t *restrict work);
void protocore_protocol_counters_get(uint8_t *restrict work);
void protocore_protocol_counters_reset(uint8_t *restrict work);
void protocore_protocol_obs_bump(uint8_t *restrict work);
void protocore_protocol_obs_transition(uint8_t *restrict work);
void protocore_protocol_obs_notice(uint8_t *restrict work);
void protocore_protocol_available(uint8_t *restrict work);
void protocore_protocol_read_byte(uint8_t *restrict work);
void protocore_protocol_peek(uint8_t *restrict work);
void protocore_protocol_consume(uint8_t *restrict work);
void protocore_protocol_read(uint8_t *restrict work);
void protocore_protocol_active(uint8_t *restrict work);
void protocore_protocol_iface(uint8_t *restrict work);
void protocore_protocol_listener_id(uint8_t *restrict work);
void protocore_protocol_tls(uint8_t *restrict work);
void protocore_protocol_owner(uint8_t *restrict work);
void protocore_protocol_proto_of(uint8_t *restrict work);
void protocore_protocol_pcb_of(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `ConnPool.set_state(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const ConnPoolNs ConnPool __attribute__((unused)) = {
    .set_state = protocore_protocol_set_state,
    .alloc_free = protocore_protocol_alloc_free,
    .timeout_ms = protocore_protocol_timeout_ms,
    .send = protocore_protocol_send,
    .send_flush = protocore_protocol_send_flush,
    .sndbuf = protocore_protocol_sndbuf,
    .flush = protocore_protocol_flush,
    .ack_consumed = protocore_protocol_ack_consumed,
    .raw_send = protocore_protocol_raw_send,
    .close = protocore_protocol_close,
    .abort_slot = protocore_protocol_abort_slot,
    .closing_finalize = protocore_protocol_closing_finalize,
    .closing_check = protocore_protocol_closing_check,
    .begin_close = protocore_protocol_begin_close,
    .enqueue = protocore_protocol_enqueue,
    .init = protocore_protocol_init,
    .stop = protocore_protocol_stop,
    .active_count = protocore_protocol_active_count,
    .remote_ip = protocore_protocol_remote_ip,
    .remote_addr = protocore_protocol_remote_addr,
    .touch_active = protocore_protocol_touch_active,
    .check_timeouts = protocore_protocol_check_timeouts,
    .on_event = protocore_protocol_on_event,
    .counters_get = protocore_protocol_counters_get,
    .counters_reset = protocore_protocol_counters_reset,
    .obs_bump = protocore_protocol_obs_bump,
    .obs_transition = protocore_protocol_obs_transition,
    .obs_notice = protocore_protocol_obs_notice,
    .available = protocore_protocol_available,
    .read_byte = protocore_protocol_read_byte,
    .peek = protocore_protocol_peek,
    .consume = protocore_protocol_consume,
    .read = protocore_protocol_read,
    .active = protocore_protocol_active,
    .iface = protocore_protocol_iface,
    .listener_id = protocore_protocol_listener_id,
    .tls = protocore_protocol_tls,
    .owner = protocore_protocol_owner,
    .proto_of = protocore_protocol_proto_of,
    .pcb_of = protocore_protocol_pcb_of,
};

/**
 * @brief The PROTOCORE_CONN_POOL_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_conn_pool_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_TCP_PROTOCOL_H
