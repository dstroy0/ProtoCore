// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file relay.h
 * @brief TCP relay / DNAT port forwarding (PC_ENABLE_RELAY) - a bidirectional byte pump.
 *
 * Publishes an internal `host:port` through the server: an inbound (accepted) connection is relayed
 * to an origin (an outbound connection to the internal service), moving bytes in both directions.
 * The engine is pure - it touches the two sockets only through send/recv seams - so it is
 * host-testable and rides `pc_client` on the device. The app drives it: each poll tick (or whenever
 * a socket is readable/writable) it calls pc_relay_step() until the relay reports DONE, then closes
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

#include "protocore_config.h"

#if PC_ENABLE_RELAY

/** @brief pc_relay_step() outcome. */
typedef enum PROTO_ENUM_PACKED
{
    PC_RELAY_ERROR = -1,  ///< a send/recv seam reported an error; the caller should close both sides
    PC_RELAY_RUNNING = 0, ///< still relaying (keep stepping)
    PC_RELAY_DONE = 1,    ///< both directions finished (EOF + drained); the caller closes both sides
} pc_relay_status;

/**
 * @brief Read up to @p cap bytes from the peer into @p buf.
 * @return bytes read (> 0), 0 if none are available now, or < 0 once the peer has closed its send
 *         side (EOF) or errored.
 */
typedef int (*pc_relay_recv_fn)(void *ctx, uint8_t *buf, size_t cap);

/**
 * @brief Write up to @p len bytes to the peer.
 * @return bytes accepted (> 0, may be < @p len under backpressure), 0 if none can be accepted right
 *         now, or < 0 on error.
 */
typedef int (*pc_relay_send_fn)(void *ctx, const uint8_t *buf, size_t len);

/** @brief Optional: signal the peer that no more data will be sent to it (a write-side half-close). */
typedef void (*pc_relay_shutdown_fn)(void *ctx);

/** @brief One end of a relay (a socket, behind seams). @p shutdown may be null. */
typedef struct
{
    pc_relay_recv_fn recv;
    pc_relay_send_fn send;
    pc_relay_shutdown_fn shutdown;
    void *ctx;
} pc_relay_end;

/** @brief A relay between two ends. Owns the per-direction carry buffers; zero heap. */
typedef struct
{
    pc_relay_end a;
    pc_relay_end b;
    uint8_t buf_a2b[PC_RELAY_BUF];
    uint8_t buf_b2a[PC_RELAY_BUF];
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
} pc_relay;

/**
 * @brief Initialize a relay between @p client (the inbound connection) and @p origin (the outbound
 *        connection to the internal service). Both ends are copied.
 */
void pc_relay_init(pc_relay *r, const pc_relay_end *client, const pc_relay_end *origin);

/**
 * @brief Do one non-blocking pass: flush any pending bytes and read more, in both directions.
 * @return a ::pc_relay_status. Call repeatedly (per poll tick) until DONE or ERROR, then close both.
 */
pc_relay_status pc_relay_step(pc_relay *r);

/**
 * @brief Signal that a peer's send side has closed, when the transport reports EOF out of band (a
 *        close callback) rather than through @c recv returning < 0.
 *
 * Some transports (e.g. the server's `pc_conn`, which delivers a close as an `on_close` event, not
 * as a short read) cannot report EOF via the recv seam. Call this from that event so the direction
 * that peer sources finishes cleanly: the bytes already buffered are still flushed, then the opposite
 * peer's `shutdown` fires and the relay reaches DONE once both directions have finished.
 *
 * @param origin false for the client (inbound) side, true for the origin (outbound) side.
 */
void pc_relay_note_eof(pc_relay *r, proto_bool origin);

#endif // PC_ENABLE_RELAY

#endif // PROTOCORE_RELAY_H
