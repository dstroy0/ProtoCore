// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file happy_eyeballs.h
 * @brief Dual-stack destination selection + Happy Eyeballs fallback (PC_ENABLE_HAPPY_EYEBALLS).
 *
 * On a dual-stack device (PC_ENABLE_IPV6), an outbound connection often has both IPv6 and IPv4
 * candidate addresses for the same host. RFC 8305 (Happy Eyeballs v2) says: sort them by RFC 6724
 * preference, interleave the families so you do not try every IPv6 before any IPv4, then start
 * connection attempts staggered by a short "Connection Attempt Delay" and take whichever connects first.
 * That gives fast IPv6 when it works and a quick fallback to IPv4 when it does not.
 *
 * This is the pure decision layer on top of the shipped `pc_ip`: a preference score, the ordering +
 * family interleave over a candidate list, and the attempt-delay gate. The app owns the sockets and the
 * DNS; this owns *which address to try next, and when*. No heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_HAPPY_EYEBALLS_H
#define PROTOCORE_HAPPY_EYEBALLS_H

#include "network_drivers/network/ip.h"
#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_HAPPY_EYEBALLS

#ifndef PC_HE_MAX
#define PC_HE_MAX 16 ///< candidate list size the interleave step handles (larger lists are only sorted).
#endif

/** @brief RFC 8305 recommended Connection Attempt Delay (ms); the spec floor is 100, default 250. */
#define PC_HE_ATTEMPT_DELAY_MS 250

/**
 * @brief RFC 6724-style preference score for a destination address (higher is tried first).
 *        Ordered by scope (global > private/ULA > link-local > loopback > multicast > unspecified),
 *        and within a scope a native IPv6 address outranks IPv4 (v4-mapped counts as IPv4).
 */
int pc_he_pref(const pc_ip *ip);

/**
 * @brief Order a candidate list for Happy Eyeballs: stable-sort by preference (desc), then interleave
 *        address families (RFC 8305 sec 4) so successive attempts alternate v6/v4 where possible.
 *        Lists longer than PC_HE_MAX are sorted but not interleaved.
 */
void pc_he_order(pc_ip *list, size_t n);

/**
 * @brief Connection Attempt Delay gate (RFC 8305 sec 5): may the next candidate's attempt start yet?
 * @return true when @p now_ms - @p last_start_ms >= @p attempt_delay_ms (wrap-safe).
 */
bool pc_he_attempt_due(uint32_t last_start_ms, uint32_t now_ms, uint32_t attempt_delay_ms);

#endif // PC_ENABLE_HAPPY_EYEBALLS
#endif // PROTOCORE_HAPPY_EYEBALLS_H
