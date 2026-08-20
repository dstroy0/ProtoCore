// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file happy_eyeballs.h
 * @brief Dual-stack destination selection + Happy Eyeballs fallback (PROTOCORE_ENABLE_HAPPY_EYEBALLS).
 *
 * On a dual-stack device (PROTOCORE_ENABLE_IPV6), an outbound connection often has both IPv6 and IPv4
 * candidate addresses for the same host. RFC 8305 (Happy Eyeballs v2) says: sort them by RFC 6724
 * preference, interleave the families so you do not try every IPv6 before any IPv4, then start
 * connection attempts staggered by a short "Connection Attempt Delay" and take whichever connects first.
 * That gives fast IPv6 when it works and a quick fallback to IPv4 when it does not.
 *
 * This is the pure decision layer on top of the shipped `protocore_ip`: a preference score, the ordering +
 * family interleave over a candidate list, and the attempt-delay gate. The app owns the sockets and the
 * DNS; this owns *which address to try next, and when*. No heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_HAPPY_EYEBALLS_H
#define PROTOCORE_HAPPY_EYEBALLS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HAPPY_EYEBALLS

PROTOCORE_BEGIN_DECLS

// PROTOCORE_HAPPY_EYEBALLS_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#ifndef PROTOCORE_HE_MAX
#define PROTOCORE_HE_MAX 16 ///< candidate list size the interleave step handles (larger lists are only sorted).
#endif

/** @brief RFC 8305 recommended Connection Attempt Delay (ms); the spec floor is 100, default 250. */
#define PROTOCORE_HE_ATTEMPT_DELAY_MS 250

#include "shared/ip/ip.h" // protocore_ip: the type a parameter points at

/** @brief What pref takes: ip. */
typedef struct
{
    const protocore_ip *ip;
} HappyEyeballsPrefArgs;
/** @brief What order takes: list, n. */
typedef struct
{
    protocore_ip *list;
    size_t n;
} HappyEyeballsOrderArgs;
/** @brief What attempt_due takes: last_start_ms, now_ms, ... */
typedef struct
{
    uint32_t last_start_ms;
    uint32_t now_ms;
    uint32_t attempt_delay_ms;
} HappyEyeballsAttemptDueArgs;
/**
 * @brief Dual-stack destination selection + Happy Eyeballs fallback (PROTOCORE_ENABLE_HAPPY_EYEBALLS).
 *
 * A caller sets the members a call takes, invokes it through ::HappyEyeballs with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   HappyEyeballs.pref_args.ip = ...;
 *   HappyEyeballs.pref(work);
 *   // HappyEyeballs.n is what the call reports
 *
 * @var HappyEyeballsNs::pref_args  what pref takes: ip
 * @var HappyEyeballsNs::order_args  what order takes: list, n
 * @var HappyEyeballsNs::attempt_due_args  what attempt_due takes: last_start_ms, now_ms,
 * @var HappyEyeballsNs::ok  true when now_ms - last_start_ms >= attempt_delay_ms (wrap-safe)
 * @var HappyEyeballsNs::n  the count a call reports
 * @var HappyEyeballsNs::pref  RFC 6724-style preference score for a destination address (higher ...
 * @var HappyEyeballsNs::order  order a candidate list for Happy Eyeballs: stable-sort by ...
 * @var HappyEyeballsNs::attempt_due  connection Attempt Delay gate (RFC 8305 sec 5): may the next ...
 *
 * @c work is PROTOCORE_HAPPY_EYEBALLS_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    HappyEyeballsPrefArgs pref_args;
    HappyEyeballsOrderArgs order_args;
    HappyEyeballsAttemptDueArgs attempt_due_args;
    proto_bool ok;
    int n;
} HappyEyeballsVars;

/** @brief The operands and the outcome. */
extern HappyEyeballsVars HappyEyeballsV;

/** @brief The entries. */
typedef struct
{
    void (*const pref)(uint8_t *restrict work);
    void (*const order)(uint8_t *restrict work);
    void (*const attempt_due)(uint8_t *restrict work);
} HappyEyeballsNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HappyEyeballsV or a region of the borrow at a fixed offset.
void protocore_happy_eyeballs_pref(uint8_t *restrict work);
void protocore_happy_eyeballs_order(uint8_t *restrict work);
void protocore_happy_eyeballs_attempt_due(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `HappyEyeballs.pref(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HappyEyeballsNs HappyEyeballs __attribute__((unused)) = {
    .pref = protocore_happy_eyeballs_pref,
    .order = protocore_happy_eyeballs_order,
    .attempt_due = protocore_happy_eyeballs_attempt_due,
};

/**
 * @brief The PROTOCORE_HAPPY_EYEBALLS_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_happy_eyeballs_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HAPPY_EYEBALLS

#endif // PROTOCORE_HAPPY_EYEBALLS_H
