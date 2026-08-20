// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file relay.h
 * @brief TCP relay / DNAT port forwarding (PROTOCORE_ENABLE_RELAY) - a bidirectional byte pump.
 *
 * Publishes an internal `host:port` through the server: an inbound (accepted) connection is relayed
 * to an origin (an outbound connection to the internal service), moving bytes in both directions.
 * The engine is pure - it touches the two sockets only through send/recv seams - so it is
 * host-testable and rides `protocore_client` on the device. The app drives it: each poll tick (or whenever
 * a socket is readable/writable) it calls protocore_relay_step() until the relay reports DONE, then closes
 * both sockets.
 *
 * Correctness details:
 *  - **Backpressure**: a `send` seam may accept fewer bytes than offered; the un-accepted bytes are
 *    carried in a per-direction buffer and retried on the next step before more are read.
 *  - **Independent half-close**: each direction finishes when its source signals EOF and its buffer
 *    drains. When a direction finishes, the opposite peer's optional `shutdown` seam is called once
 *    (propagating the half-close so the origin sees the client's FIN); the relay is DONE only when
 *    both directions have finished.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RELAY_H
#define PROTOCORE_RELAY_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_RELAY

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief protocore_relay_step() outcome. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_RELAY_ERROR = -1,  ///< a send/recv seam reported an error; the caller should close both sides
    PROTOCORE_RELAY_RUNNING = 0, ///< still relaying (keep stepping)
    PROTOCORE_RELAY_DONE = 1,    ///< both directions finished (EOF + drained); the caller closes both sides
} protocore_relay_status;

/**
 * @brief Read up to @p cap bytes from the peer into @p buf.
 * @return bytes read (> 0), 0 if none are available now, or < 0 once the peer has closed its send
 *         side (EOF) or errored.
 */
typedef int (*protocore_relay_recv_fn)(void *ctx, uint8_t *buf, size_t cap);

/**
 * @brief Write up to @p len bytes to the peer.
 * @return bytes accepted (> 0, may be < @p len under backpressure), 0 if none can be accepted right
 *         now, or < 0 on error.
 */
typedef int (*protocore_relay_send_fn)(void *ctx, const uint8_t *buf, size_t len);

/** @brief Optional: signal the peer that no more data will be sent to it (a write-side half-close). */
typedef void (*protocore_relay_shutdown_fn)(void *ctx);

/** @brief One end of a relay (a socket, behind seams). @p shutdown may be null. */
typedef struct
{
    protocore_relay_recv_fn recv;
    protocore_relay_send_fn send;
    protocore_relay_shutdown_fn shutdown;
    void *ctx;
} protocore_relay_end;
/** @brief A relay between two ends. Owns the per-direction carry buffers; zero heap. */
typedef struct
{
    protocore_relay_end a;
    protocore_relay_end b;
    uint8_t buf_a2b[PROTOCORE_RELAY_BUF];
    uint8_t buf_b2a[PROTOCORE_RELAY_BUF];
    uint16_t a2b_len; ///< bytes read from a pending send to b
    uint16_t a2b_off; ///< how many of those already sent
    uint16_t b2a_len;
    uint16_t b2a_off;
    proto_bool a_eof;       ///< the recv side of a has hit EOF
    proto_bool b_eof;       ///< the recv side of b has hit EOF
    proto_bool a2b_done;    ///< the a->b direction has finished (EOF + drained)
    proto_bool b2a_done;    ///< the b->a direction has finished (EOF + drained)
    proto_bool a_shut_sent; ///< the shutdown seam of a has been called
    proto_bool b_shut_sent; ///< the shutdown seam of b has been called
    uint32_t bytes_a2b;     ///< bytes relayed a->b (observability)
    uint32_t bytes_b2a;     ///< bytes relayed b->a (observability)
} protocore_relay;
/** @brief What init takes: r, client, origin. */
typedef struct
{
    protocore_relay *r;
    const protocore_relay_end *client;
    const protocore_relay_end *origin;
} RelayInitArgs;
/** @brief What step takes: r. */
typedef struct
{
    protocore_relay *r;
} RelayStepArgs;
/** @brief What note_eof takes: r, origin. */
typedef struct
{
    protocore_relay *r;
    proto_bool origin; ///< false for the client (inbound) side, true for the origin (outbound) side
} RelayNoteEofArgs;
/**
 * @brief TCP relay / DNAT port forwarding (PROTOCORE_ENABLE_RELAY) - a bidirectional byte pump. Publishes an internal
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::Relay with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Relay.init_args.r = ...;
 *   Relay.init_args.client = ...;
 *   Relay.init_args.origin = ...;
 *   Relay.init(work);
 *
 * @var RelayNs::init_args  what init takes: r, client, origin
 * @var RelayNs::step_args  what step takes: r
 * @var RelayNs::note_eof_args  what note_eof takes: r, origin
 * @var RelayNs::ok  a call's true/false outcome
 * @var RelayNs::status  a ::protocore_relay_status. Call repeatedly (per poll tick) until ...
 * @var RelayNs::init  initialize a relay between client (the inbound connection) and ...
 * @var RelayNs::step  do one non-blocking pass: flush any pending bytes and read more, in ...
 * @var RelayNs::note_eof  signal that a peer's send side has closed, when the transport ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    RelayInitArgs init_args;
    RelayStepArgs step_args;
    RelayNoteEofArgs note_eof_args;
    proto_bool ok;
    protocore_relay_status status;
} RelayVars;

/** @brief The operands and the outcome. */
extern RelayVars RelayV;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const step)(uint8_t *restrict work);
    void (*const note_eof)(uint8_t *restrict work);
} RelayNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in RelayV or a region of the borrow at a fixed offset.
void protocore_relay_init(uint8_t *restrict work);
void protocore_relay_step(uint8_t *restrict work);
void protocore_relay_note_eof(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Relay.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const RelayNs Relay __attribute__((unused)) = {
    .init = protocore_relay_init,
    .step = protocore_relay_step,
    .note_eof = protocore_relay_note_eof,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RELAY

#endif // PROTOCORE_RELAY_H
