// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file safety_scl.h
 * @brief IEC 61784-3 black-channel Safety Communication Layer - the shared primitives
 *        (PC_ENABLE_SAFETY_SCL).
 *
 * The functional-safety profiles (PROFIsafe / IEC 61784-3-3, CIP Safety / -3-2, FSoE / -3-12,
 * IO-Link Safety) all work the same way: they treat the underlying fieldbus as an untrusted "black
 * channel" and layer their own end-to-end checks on top, so the transport may corrupt, lose,
 * duplicate, delay or reorder frames without defeating the safety function. Three mechanisms do that
 * work, and they are common to every profile:
 *
 *   1. a **CRC signature** over the safety payload, sized so the residual error rate meets SIL 3,
 *   2. a **monitoring / consecutive counter**, which turns lost, repeated, inserted and reordered
 *      frames into a detectable mismatch, and
 *   3. a **receive watchdog**, so a silent channel is itself a fault rather than a stale-but-happy
 *      reading.
 *
 * This module lands (2) and (3) plus the fail-safe state machine that combines all three, so each
 * profile's codec composes them instead of reimplementing them.
 *
 * **What this module deliberately does not do: compute the CRC.** Every profile defines its own
 * polynomial, width, seed, and the order in which payload / counter / connection identifier are fed
 * into it, and those constants live in paid standards. Encoding a guess here would produce something
 * that looks authoritative and silently fails to detect the very corruptions it exists to catch -
 * the worst possible outcome for a safety layer. So the caller computes its profile's CRC and passes
 * the verdict in as @p signature_ok. What this module owns is the *consequence* of that verdict,
 * which genuinely is profile-independent.
 *
 * **Fail-safe latches.** Once any check fails the connection enters @ref SCL_STATE_FAILSAFE and stays
 * there until an explicit @ref pc_scl_reset. A safety layer must never silently reheal: recovering
 * on its own would let an intermittent fault present as a working connection, which is precisely the
 * failure a SIL rating is meant to exclude. Re-establishing is an operator/application decision.
 *
 * Pure, with an explicit @p now (the `services/hotswap` pattern - it decides, the binding acts), so
 * the whole machine is host-testable against a synthetic clock. Every elapsed-time test is an
 * unsigned difference, so it is wrap-safe across a `millis()` rollover. No heap, no stdlib.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SAFETY_SCL_H
#define PROTOCORE_SAFETY_SCL_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SAFETY_SCL

/** @brief Where a safety connection stands. */
typedef enum PROTO_ENUM_PACKED
{
    SCL_STATE_INIT = 0,     ///< initialized, no valid frame accepted yet.
    SCL_STATE_RUNNING = 1,  ///< at least one frame accepted and no fault since.
    SCL_STATE_FAILSAFE = 2, ///< a check failed; latched until pc_scl_reset().
} SclState;

/** @brief Why a connection went fail-safe. */
typedef enum PROTO_ENUM_PACKED
{
    SCL_FAULT_PC_NONE = 0,   ///< no fault.
    SCL_FAULT_SIGNATURE = 1, ///< the caller's CRC check rejected the frame (corruption).
    SCL_FAULT_COUNTER = 2,   ///< the monitoring counter was not the expected next value.
    SCL_FAULT_TIMEOUT = 3,   ///< no valid frame arrived within the watchdog.
} SclFault;

/**
 * @brief One safety connection's receive state.
 *
 * @ref counter_mod is the monitoring counter's wrap modulus - profiles use narrow counters (an
 * 8-bit consecutive number is common), so the expected value must wrap the same way the sender's
 * does or the two desynchronize after the first wrap. 0 means the full 32-bit range.
 */
typedef struct
{
    SclState state;       ///< current state.
    SclFault fault;       ///< why it went fail-safe (PC_NONE unless FAILSAFE).
    uint32_t expected;    ///< monitoring counter value the next frame must carry.
    uint32_t counter_mod; ///< counter wrap modulus (0 = full 32-bit range).
    uint32_t watchdog_ms; ///< maximum gap between accepted frames (0 disables the watchdog).
    uint32_t last_ok_ms;  ///< when the last frame was accepted.
    uint32_t accepted;    ///< frames accepted since init.
    uint32_t rejected;    ///< frames rejected since init.
} SclConn;

/**
 * @brief Initialize a connection to @ref SCL_STATE_INIT at @p now.
 *
 * @param first_counter the monitoring counter value the first frame is required to carry.
 * @param counter_mod   counter wrap modulus; 0 for the full 32-bit range.
 * @param watchdog_ms   maximum gap between accepted frames; 0 disables the watchdog.
 */
void pc_scl_init(SclConn *c, uint32_t first_counter, uint32_t counter_mod, uint32_t watchdog_ms, uint32_t now);

/**
 * @brief Offer one received frame to the connection.
 *
 * @param signature_ok the profile's CRC verdict for this frame - see the file comment on why this
 *                     module does not compute it.
 * @param counter      the monitoring counter carried by the frame.
 *
 * A frame is accepted only when the signature is good AND the counter is exactly the expected next
 * value. Every way the black channel can misbehave shows up here: a lost frame or an inserted one
 * makes the counter run ahead, a duplicate makes it repeat, and a reordered pair makes it go
 * backwards - all of which are simply "not the expected value", which is what makes one comparison
 * sufficient. Any failure latches @ref SCL_STATE_FAILSAFE.
 *
 * Offering a frame to an already-fail-safe connection is refused without changing the recorded
 * fault: the first fault is the diagnostically interesting one.
 *
 * @return true if the frame was accepted (its safety payload may be used).
 */
proto_bool pc_scl_on_frame(SclConn *c, proto_bool signature_ok, uint32_t counter, uint32_t now);

/**
 * @brief Check the receive watchdog at @p now. Call it every cycle, including cycles with no frame -
 *        a silent channel is exactly what this detects.
 *
 * The watchdog only runs once a connection is @ref SCL_STATE_RUNNING: a connection that has not yet
 * received its first frame is not yet timing out, it is still starting up.
 *
 * @return true if the connection is still usable (not fail-safe).
 */
proto_bool pc_scl_poll(SclConn *c, uint32_t now);

/**
 * @brief Re-establish a fail-safe connection, arming it for @p first_counter at @p now.
 *
 * Deliberately explicit: see the file comment on why the layer never rehabilitates itself.
 * Counters are preserved so the accepted/rejected tallies span the whole session.
 */
void pc_scl_reset(SclConn *c, uint32_t first_counter, uint32_t now);

/** @brief True while the connection may be used (not fail-safe). */
proto_bool pc_scl_ok(const SclConn *c);

/** @brief The connection's state. */
SclState pc_scl_state(const SclConn *c);

/** @brief Why the connection went fail-safe (@ref SCL_FAULT_PC_NONE unless it did). */
SclFault pc_scl_fault(const SclConn *c);

/**
 * @brief Advance a *sender's* monitoring counter, honouring the same wrap modulus.
 *
 * Provided so a sender and its peer receiver cannot disagree about wrapping - the commonest way a
 * hand-rolled consecutive number desynchronizes after its first wrap.
 *
 * @return the next counter value to put on the wire.
 */
uint32_t pc_scl_next_counter(uint32_t counter, uint32_t counter_mod);

#endif // PC_ENABLE_SAFETY_SCL

PROTO_END_DECLS

#endif // PROTOCORE_SAFETY_SCL_H
