// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file safety_scl.c
 * @brief IEC 61784-3 black-channel SCL shared primitives - implementation. See safety_scl.h.
 *
 * The whole module is one small state machine, and every transition out of RUNNING is one-way. The
 * elapsed-time test is an unsigned difference (`now - stamp >= limit`), which is what makes the
 * watchdog wrap-safe: at a millis() rollover the subtraction wraps with it and still yields the true
 * interval.
 */

#include "services/machine_tool/safety_scl/safety_scl.h"

#if PC_ENABLE_SAFETY_SCL

// Latch the first fault: once fail-safe, later failures do not overwrite the diagnostically
// interesting one that actually broke the connection.
static void trip(SclConn *c, SclFault why)
{
    // Both callers (pc_scl_on_frame, pc_scl_poll) already early-return when state == FAILSAFE, so
    // trip() is never entered in that state; the guard latches the first fault as belt-and-suspenders.
    if (c->state != SCL_STATE_FAILSAFE) // GCOVR_EXCL_BR_LINE  false arm unreachable (see above)
    {
        c->state = SCL_STATE_FAILSAFE;
        c->fault = why;
    }
}

static uint32_t wrap(uint32_t v, uint32_t mod)
{
    return mod ? (v % mod) : v;
}

void pc_scl_init(SclConn *c, uint32_t first_counter, uint32_t counter_mod, uint32_t watchdog_ms, uint32_t now)
{
    if (!c)
    {
        return;
    }
    c->state = SCL_STATE_INIT;
    c->fault = SCL_FAULT_PC_NONE;
    c->counter_mod = counter_mod;
    c->expected = wrap(first_counter, counter_mod);
    c->watchdog_ms = watchdog_ms;
    c->last_ok_ms = now;
    c->accepted = 0;
    c->rejected = 0;
}

proto_bool pc_scl_on_frame(SclConn *c, proto_bool signature_ok, uint32_t counter, uint32_t now)
{
    if (!c)
    {
        return PROTO_FALSE;
    }
    if (c->state == SCL_STATE_FAILSAFE)
    {
        c->rejected++; // refused, but the original fault stands
        return PROTO_FALSE;
    }

    // Corruption is checked first: a frame whose signature failed cannot be trusted to carry a
    // meaningful counter either, so reporting SIGNATURE is the honest diagnosis.
    if (!signature_ok)
    {
        c->rejected++;
        trip(c, SCL_FAULT_SIGNATURE);
        return PROTO_FALSE;
    }
    if (wrap(counter, c->counter_mod) != c->expected)
    {
        c->rejected++;
        trip(c, SCL_FAULT_COUNTER);
        return PROTO_FALSE;
    }

    c->expected = pc_scl_next_counter(c->expected, c->counter_mod);
    c->last_ok_ms = now;
    c->accepted++;
    c->state = SCL_STATE_RUNNING;
    return PROTO_TRUE;
}

proto_bool pc_scl_poll(SclConn *c, uint32_t now)
{
    if (!c)
    {
        return PROTO_FALSE;
    }
    if (c->state == SCL_STATE_FAILSAFE)
    {
        return PROTO_FALSE;
    }
    // Only a connection that has actually run can time out; one still in INIT is starting up, not
    // silent. A zero watchdog disables the check entirely.
    if (c->state == SCL_STATE_RUNNING && c->watchdog_ms && (now - c->last_ok_ms) >= c->watchdog_ms)
    {
        trip(c, SCL_FAULT_TIMEOUT);
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

void pc_scl_reset(SclConn *c, uint32_t first_counter, uint32_t now)
{
    if (!c)
    {
        return;
    }
    c->state = SCL_STATE_INIT;
    c->fault = SCL_FAULT_PC_NONE;
    c->expected = wrap(first_counter, c->counter_mod);
    c->last_ok_ms = now;
    // accepted / rejected deliberately preserved: they tally the whole session, not one connection
    // attempt, so a flapping link is visible rather than reset away.
}

proto_bool pc_scl_ok(const SclConn *c)
{
    return c && c->state != SCL_STATE_FAILSAFE;
}

SclState pc_scl_state(const SclConn *c)
{
    return c ? c->state : SCL_STATE_FAILSAFE; // a missing connection is not a usable one
}

SclFault pc_scl_fault(const SclConn *c)
{
    return c ? c->fault : SCL_FAULT_PC_NONE;
}

uint32_t pc_scl_next_counter(uint32_t counter, uint32_t counter_mod)
{
    uint32_t n = counter + 1u; // wraps naturally at 2^32 when no modulus is set
    return counter_mod ? (n % counter_mod) : n;
}

#endif // PC_ENABLE_SAFETY_SCL
