// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file signaling.h
 * @brief Application layer signaling: the bucket the server's state is read from, and the one way a
 *        connection the application layer owns is killed.
 *
 * Signaling in the control-plane sense - what the server knows about itself, kept apart from the data
 * it is moving.
 *
 * **Signaling owns no state, and it never gathers any.** It originates nothing. The active function
 * in the server loop already knows each fact at the instant it becomes true, so it deposits it here
 * then, and only when there is something to deposit. ::SignalingNs::know hands the bucket back; it does
 * not compose, poll, or ask an owner for anything.
 *
 * Both halves of that matter. A bucket that gathered on read would recompute what the loop had
 * already established, and would interleave reads of owners that are moving underneath it. A bucket
 * that owned its fields would be a second tally beside the real one, drifting the first time an owner
 * changed without telling it. Depositing at the point of truth is neither: the fact is written once,
 * by the code that had it.
 *
 * The problem it solves is that the state had no single place to be read from. protocore_stats(),
 * protocore_metrics(), and protocore_diag() each walk a different set of owners and assemble their own picture, so
 * the same question already has three answers, and a fourth reader would write a fourth.
 *
 * **Kill, for applications that do not talk transport.** An application at this layer has no
 * transport dependency and should not acquire one just to hang up: reaching for Tcp.conn->close() would
 * put an L4 include in L7 code and make every such application know about slots and PCBs. This is the
 * seam instead. The decision is the application's and the teardown is the transport's.
 *
 * It also lets a remote kill its own connection - and only its own, because a remote request arrives
 * on its slot and never supplies a slot number, so it has no way to name another. That is structural
 * rather than a check, which is why there is no permission test here and no way to forge past one.
 *
 * A module that already speaks transport keeps calling transport directly. This does not replace
 * Tcp.conn->close(); it means an application never has to reach for it.
 *
 * This is not SSH signaling. RFC 4254 signaling delivers a POSIX signal to a remote process over a
 * channel and is implemented in ssh_flow_control; the two share a word and nothing else.
 *
 * Single-accessor like the rest of the server: use it from the worker that owns the slot.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SIGNALING_H
#define PROTOCORE_SIGNALING_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief The server's state, as the loop deposited it.
 *
 * A struct rather than a set of getters so a reader takes one consistent picture in one call instead
 * of interleaving reads while the loop runs between them.
 */
typedef struct
{
    uint32_t uptime_ms;      ///< Milliseconds the server has been up.
    uint32_t requests_total; ///< Responses sent.
    uint32_t responses_2xx;
    uint32_t responses_4xx;
    uint32_t responses_5xx;

    // Masks, not counts. Which slot and which listener is the fact the pools already hold, and a
    // count throws it away: __builtin_popcount recovers the tally from the mask in one instruction,
    // while nothing recovers the identity from a tally. It is the shape the pools are allocated with
    // (protocore_conn_alloc_free in tcp.c, the SFTP handle table), so a reader comparing the bucket against
    // the pool is comparing like with like.
    uint32_t conns_active; ///< One bit per connection slot in use.
    uint32_t listeners_up; ///< One bit per bound listener.
} protocore_signal_snapshot;

static_assert(CONN_POOL_SLOTS <= 32, "protocore_signal_snapshot::conns_active is one 32-bit word, one bit per slot");
static_assert(MAX_LISTENERS <= 32, "protocore_signal_snapshot::listeners_up is one 32-bit word, one bit per listener");

/** @brief What one deposit carries: a response's status, or the loop's own figures. */
typedef struct
{
    int code;              ///< the status that was sent, which is what selects the class tally
    uint32_t uptime_ms;    ///< how long the server has been up
    uint32_t conns_active; ///< one bit per occupied connection slot
    uint32_t listeners_up; ///< one bit per listener that is bound
} SignalPutArgs;

/** @brief The bucket's own state and the calls that reach it, described only in signaling.c. */
struct SignalingInternal;

/**
 * @brief The server's signalling bucket.
 *
 * A caller sets the members a call takes, invokes it through ::Signal, and reads the outcome off the
 * same handle. The bucket itself is behind @ref internal.
 *
 * @var SignalingNs::put           what one deposit carries
 * @var SignalingNs::slot          the connection a kill names
 * @var SignalingNs::out           where a read copies the bucket
 * @var SignalingNs::know          copy the bucket out; no gathering, this is what the loop deposited
 * @var SignalingNs::reset         empty the bucket
 * @var SignalingNs::put_response  deposit a response, from the send path, as the status goes out
 * @var SignalingNs::put_tick      deposit what the loop iteration already established
 * @var SignalingNs::kill          end a connection, for an application that does not talk transport
 * @var SignalingNs::internal      the bucket and the calls that reach it
 *
 * know copies rather than handing out a pointer: a reader formats several fields and the loop
 * deposits between its reads, so lending the storage would let one report mix two server states.
 *
 * put_tick is one call rather than three because these arrive together: the loop reads the clock
 * every iteration for the idle-timeout sweep and walks the slots to service them, and the listener
 * pool is touched constantly. Every value is in hand at the moment of the call, so the deposit costs
 * the stores and nothing else.
 *
 * kill is a plain forward: no liveness test, no result. Transport owns the slot's lifetime and its
 * idle sweep reaps a stale one regardless, so a check here would answer a question transport has
 * already answered, and the answer could be stale before the caller read it.
 */
typedef struct
{
    SignalPutArgs put;
    uint8_t slot;
    protocore_signal_snapshot *out;

    void (*know)(struct SignalingInternal *ctx);
    void (*reset)(struct SignalingInternal *ctx);
    void (*put_response)(struct SignalingInternal *ctx);
    void (*put_tick)(struct SignalingInternal *ctx);
    void (*kill)(struct SignalingInternal *ctx);

    struct SignalingInternal *internal;
} SignalingNs;

/** @brief The one symbol this module exports. */
extern SignalingNs Signal;

PROTOCORE_END_DECLS

#endif // PROTOCORE_SIGNALING_H
