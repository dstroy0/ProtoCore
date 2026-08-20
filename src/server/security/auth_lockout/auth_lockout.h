// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file auth_lockout.h
 * @brief Per-source-address authentication lockout (PROTOCORE_ENABLE_AUTH_LOCKOUT).
 *
 * A small fixed table of buckets, one per recently-seen source address: consecutive failures
 * escalate a lockout, a success clears it, and the least recently used bucket is evicted when the
 * table is full. The window arithmetic is unsigned throughout, so a millisecond-counter rollover
 * cannot unlock an address early or lock one out forever.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AUTH_LOCKOUT_H
#define PROTOCORE_AUTH_LOCKOUT_H

#include "protocore_config.h" // the entry point: protocore_ip, and the widths

#if PROTOCORE_ENABLE_AUTH_LOCKOUT

PROTOCORE_BEGIN_DECLS

// PROTOCORE_AUTH_LOCKOUT_BORROW - the bytes the table lives in - is stated in protocore_config.h,
// which sums it into the secure arena. A caller takes them once for the life of the program and
// passes the pointer to every call. How they are carved is this module's and is never named here.

/** @brief The source address a call is about; see shared/ip/ip.h. */
struct protocore_ip;

/** @brief The address a call is about, and the clock it is asked at. */
typedef struct
{
    const struct protocore_ip *ip; ///< the source address; unspecified addresses are untrackable
    uint32_t now_ms;               ///< the caller's monotonic clock, so this module keeps none
} AuthLockoutArgs;

/**
 * @brief Per-source-address authentication lockout.
 *
 * A caller sets the members a call takes, invokes it through ::AuthLockout with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   AuthLockout.args.ip = &peer;
 *   AuthLockout.args.now_ms = Clock.ms;
 *   AuthLockout.remaining(work);
 *   // AuthLockout.ms is 0 when the address may try again
 *
 * @var AuthLockoutNs::args       the address a call is about, and the clock it is asked at
 * @var AuthLockoutNs::ok         a call's true/false outcome
 * @var AuthLockoutNs::ms         milliseconds until the lockout expires; 0 when not locked out
 * @var AuthLockoutNs::remaining  read the remaining lockout for the address
 * @var AuthLockoutNs::fail       record a failed authentication; may start or escalate a lockout
 * @var AuthLockoutNs::succeed    clear the address's failure and lockout state
 * @var AuthLockoutNs::reset      empty the whole table
 *
 * @c work is PROTOCORE_AUTH_LOCKOUT_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The table is in
 * those bytes rather than in this module, so a caller takes them once for the life of the program
 * and every call runs out of the same span. The caller releases it, and the pool wipes on release;
 * this module neither takes it, holds it, nor releases it.
 *
 * No storage member and no context: a caller sets operands and reads @ref AuthLockoutNs::ok, and
 * that is all the surface there is.
 */
typedef struct
{
    AuthLockoutArgs args;
    proto_bool ok;
    uint32_t ms;
} AuthLockoutVars;

/** @brief The operands and the outcome. */
extern AuthLockoutVars AuthLockoutV;

/** @brief The entries. */
typedef struct
{
    void (*const remaining)(uint8_t *restrict work);
    void (*const fail)(uint8_t *restrict work);
    void (*const succeed)(uint8_t *restrict work);
    void (*const reset)(uint8_t *restrict work);
} AuthLockoutNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in AuthLockoutV or a region of the borrow at a fixed offset.
void protocore_auth_lockout_remaining(uint8_t *restrict work);
void protocore_auth_lockout_fail(uint8_t *restrict work);
void protocore_auth_lockout_succeed(uint8_t *restrict work);
void protocore_auth_lockout_reset(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `AuthLockout.remaining(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const AuthLockoutNs AuthLockout __attribute__((unused)) = {
    .remaining = protocore_auth_lockout_remaining,
    .fail = protocore_auth_lockout_fail,
    .succeed = protocore_auth_lockout_succeed,
    .reset = protocore_auth_lockout_reset,
};

/**
 * @brief The PROTOCORE_AUTH_LOCKOUT_BORROW bytes the program's table lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where that
 * borrow comes from. Taken once from the end of the secure pool, which no mark and no release walks,
 * so the table lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_auth_lockout_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AUTH_LOCKOUT

#endif // PROTOCORE_AUTH_LOCKOUT_H
