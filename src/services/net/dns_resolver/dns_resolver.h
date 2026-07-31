// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_resolver.h
 * @brief DNS resolver with answer verification (PC_ENABLE_DNS_RESOLVER).
 *
 * Resolves a hostname to an IPv4 address via lwIP (dns_gethostbyname, marshalled
 * to tcpip_thread like the http_client), and classifies / verifies the answer:
 * a remote name resolving to 0.0.0.0, the broadcast address, loopback, or a
 * multicast address is rejected as a spoof / DNS-rebinding indicator. The
 * classifier + verifier are pure and host-tested; the resolve is ESP32-only and
 * blocking (call it off the request hot path).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNS_RESOLVER_H
#define PROTOCORE_DNS_RESOLVER_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_NEED_DNS_RESOLVER

/** @brief IPv4 address category (RFC special-purpose ranges). */
enum class pc_ip_class : uint8_t
{
    PC_IP_UNSPECIFIED = 0, ///< 0.0.0.0
    PC_IP_LOOPBACK,        ///< 127.0.0.0/8
    PC_IP_PRIVATE,         ///< 10/8, 172.16/12, 192.168/16
    PC_IP_LINKLOCAL,       ///< 169.254.0.0/16
    PC_IP_MULTICAST,       ///< 224.0.0.0/4
    PC_IP_BROADCAST,       ///< 255.255.255.255
    PC_IP_PUBLIC,          ///< globally-routable unicast
};

// ---------------------------------------------------------------------------
// Host-testable core
// ---------------------------------------------------------------------------

/** @brief Classify a host-order IPv4 word (e.g. (10u << 24) | (0u << 16) | (0u << 8) | 1u). */
pc_ip_class pc_dns_resolver_classify(uint32_t ip);

/**
 * @brief Is @p ip a plausible A-record answer for a remote host?
 *
 * Rejects unspecified / broadcast / loopback / multicast (spoof / rebinding
 * indicators); accepts private / link-local / public. Host order.
 */
bool pc_dns_resolver_verify(uint32_t ip);

// ---------------------------------------------------------------------------
// Resolve (ESP32; returns false on host)
// ---------------------------------------------------------------------------

/**
 * @brief Resolve @p host to an IPv4 address (host order) into @p out_ip.
 *
 * Accepts a dotted-quad directly; otherwise queries DNS with a
 * PC_DNS_TIMEOUT_MS deadline. Blocking. @return true on success.
 */
bool pc_dns_resolver_resolve(const char *host, uint32_t *out_ip);

/**
 * @brief Resolve @p host and require the answer to pass pc_dns_resolver_verify().
 * @return true only if it resolved AND the address is a plausible answer.
 */
bool pc_dns_resolver_resolve_verified(const char *host, uint32_t *out_ip);

#if !defined(ARDUINO)
/** @brief Host test hook: make pc_dns_resolver_resolve() return @p ip (host order) when @p ok, else fail. */
void pc_dns_resolver_test_set_resolve(bool ok, uint32_t ip);
#endif

#endif // PC_NEED_DNS_RESOLVER
#endif // PROTOCORE_DNS_RESOLVER_H
