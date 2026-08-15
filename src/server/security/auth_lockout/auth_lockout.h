// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file auth_lockout.h
 * @brief Per-peer brute-force lockout for HTTP auth (PROTOCORE_ENABLE_AUTH_LOCKOUT).
 *
 * Tracks consecutive failed authentications per source address in a fixed BSS
 * table (no heap). The key is the full family-tagged address (protocore_ip), so an IPv4
 * and an IPv6 peer are always distinct buckets and no attacker can share or poison
 * another address's state through a lossy hash collision. After
 * PROTOCORE_AUTH_LOCKOUT_THRESHOLD consecutive failures an address is locked out for
 * PROTOCORE_AUTH_LOCKOUT_BASE_MS, doubling on each further failure up to
 * PROTOCORE_AUTH_LOCKOUT_MAX_MS; a successful auth clears the address. Compiled only
 * when PROTOCORE_ENABLE_AUTH_LOCKOUT is set (the host unit tests enable it and drive
 * it with a synthetic millisecond clock). An unspecified address (family
 * protocore_ip_family::PROTOCORE_IP_NONE or all-zero) is untrackable and is never locked.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AUTH_LOCKOUT_H
#define PROTOCORE_AUTH_LOCKOUT_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_AUTH_LOCKOUT

#include "shared/ip/ip.h"

/**
 * @brief Remaining lockout time for @p ip at @p now_ms, in milliseconds.
 *
 * @return 0 if the address is not currently locked out (or is unspecified /
 *         untrackable); otherwise the milliseconds until the lockout expires. The
 *         window math is unsigned so it survives a millis() rollover.
 */
uint32_t auth_lockout_remaining_ms(const protocore_ip *ip, uint32_t now_ms);

/** @brief Record a failed authentication from @p ip at @p now_ms (may start or escalate a lockout). */
void auth_lockout_fail(const protocore_ip *ip, uint32_t now_ms);

/** @brief Clear @p ip's failure / lockout state after a successful authentication. */
void auth_lockout_succeed(const protocore_ip *ip);

/** @brief Reset the whole lockout table (e.g. between tests). */
void auth_lockout_reset(void);

#endif // PROTOCORE_ENABLE_AUTH_LOCKOUT

#endif // PROTOCORE_AUTH_LOCKOUT_H
