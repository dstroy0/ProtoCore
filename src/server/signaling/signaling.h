// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * then, and only when there is something to deposit. ::pc_signal_know hands the bucket back; it does
 * not compose, poll, or ask an owner for anything.
 *
 * Both halves of that matter. A bucket that gathered on read would recompute what the loop had
 * already established, and would interleave reads of owners that are moving underneath it. A bucket
 * that owned its fields would be a second tally beside the real one, drifting the first time an owner
 * changed without telling it. Depositing at the point of truth is neither: the fact is written once,
 * by the code that had it.
 *
 * The problem it solves is that the state had no single place to be read from. pc_stats(),
 * pc_metrics(), and pc_diag() each walk a different set of owners and assemble their own picture, so
 * the same question already has three answers, and a fourth reader would write a fourth.
 *
 * **Kill, for applications that do not talk transport.** An application at this layer has no
 * transport dependency and should not acquire one just to hang up: reaching for pc_conn_close() would
 * put an L4 include in L7 code and make every such application know about slots and PCBs. This is the
 * seam instead. The decision is the application's and the teardown is the transport's.
 *
 * It also lets a remote kill its own connection - and only its own, because a remote request arrives
 * on its slot and never supplies a slot number, so it has no way to name another. That is structural
 * rather than a check, which is why there is no permission test here and no way to forge past one.
 *
 * A module that already speaks transport keeps calling transport directly. This does not replace
 * pc_conn_close(); it means an application never has to reach for it.
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

PROTO_BEGIN_DECLS

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
    // (pc_conn_alloc_free in tcp.c, the SFTP handle table), so a reader comparing the bucket against
    // the pool is comparing like with like.
    uint32_t conns_active; ///< One bit per connection slot in use.
    uint32_t listeners_up; ///< One bit per bound listener.
} pc_signal_snapshot;

static_assert(CONN_POOL_SLOTS <= 32, "pc_signal_snapshot::conns_active is one 32-bit word, one bit per slot");
static_assert(MAX_LISTENERS <= 32, "pc_signal_snapshot::listeners_up is one 32-bit word, one bit per listener");

/** @brief Hand back the bucket. No gathering: this is what the loop last deposited. */
void pc_signal_know(pc_signal_snapshot *out);

/** @brief Empty the bucket. See pc_server_reset(), which is what callers use. */
void pc_signal_reset(void);

/**
 * @brief Deposit a response, from the send path, at the point the status went out.
 *
 * @param code the status that was sent, which is what selects the class tally.
 */
void pc_signal_put_response(int code);

/**
 * @brief Deposit what the loop iteration already established.
 *
 * One call rather than three, because these arrive together: the loop reads the clock every
 * iteration for the idle-timeout sweep and walks the slots to service them, and the listener pool is
 * touched constantly (24 sites across 7 files, including the dynamic listeners SSH remote-forwarding
 * opens). Every value here is in hand at the moment of the call, so the deposit costs the stores and
 * nothing else. Splitting it would carry the same facts across the boundary three times.
 */
void pc_signal_put_tick(uint32_t uptime_ms, uint32_t conns_active, uint32_t listeners_up);

/**
 * @brief End connection @p slot, for an application that does not talk transport.
 *
 * Resolves to the transport's teardown, so an application states the decision without taking on an
 * L4 dependency to carry it out. A remote reaches this on the slot its own request arrived on.
 */
void pc_signal_kill(uint8_t slot);

PROTO_END_DECLS

#endif // PROTOCORE_SIGNALING_H
