// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file resolver.h
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

#if PC_NEED_DNS_RESOLVER

PROTO_BEGIN_DECLS

/** @brief IPv4 address category (RFC special-purpose ranges). */
typedef enum PROTO_ENUM_PACKED
{
    PC_IP_UNSPECIFIED = 0, ///< 0.0.0.0
    PC_IP_LOOPBACK,        ///< 127.0.0.0/8
    PC_IP_PRIVATE,         ///< 10/8, 172.16/12, 192.168/16
    PC_IP_LINKLOCAL,       ///< 169.254.0.0/16
    PC_IP_MULTICAST,       ///< 224.0.0.0/4
    PC_IP_BROADCAST,       ///< 255.255.255.255
    PC_IP_PUBLIC,          ///< globally-routable unicast
} pc_ip_class;

// ---------------------------------------------------------------------------
// Host-testable core
// ---------------------------------------------------------------------------

/** @brief Classify a host-order IPv4 word (e.g. (10u << 24) | (0u << 16) | (0u << 8) | 1u). */

/**
 * @brief Is @p ip a plausible A-record answer for a remote host?
 *
 * Rejects unspecified / broadcast / loopback / multicast (spoof / rebinding
 * indicators); accepts private / link-local / public. Host order.
 */

// ---------------------------------------------------------------------------
// Resolve (ESP32; returns false on host)
// ---------------------------------------------------------------------------

/**
 * @brief Resolve @p host to an IPv4 address (host order) into @p out_ip.
 *
 * Accepts a dotted-quad directly; otherwise queries DNS with a
 * PC_DNS_TIMEOUT_MS deadline. Blocking. @return true on success.
 */

/**
 * @brief Resolve @p host and require the answer to pass @ref ResolverNs::verify.
 * @return true only if it resolved AND the address is a plausible answer.
 */

#if !PROTOCORE_HOT
/** @brief Host test hook: make @ref ResolverNs::resolve return @p ip (host order) when @p ok, else fail. */

#endif

/**
 * @brief The DNS resolver.
 *
 * @var ResolverNs::classify  what kind of address a host-order IPv4 word names
 * @var ResolverNs::verify    whether that word is a plausible A-record answer for a remote host
 * @var ResolverNs::resolve   resolve a host to an IPv4 address, host order; blocking
 * @var ResolverNs::resolve_verified  resolve, and require the answer to pass @ref ResolverNs::verify
 *
 * No storage member: the deadline and the in-flight call belong to dns_resolver.c, and a caller
 * reaches them by calling.
 */
typedef struct
{
    pc_ip_class (*classify)(uint32_t ip);
    proto_bool (*verify)(uint32_t ip);
    proto_bool (*resolve)(const char *host, uint32_t *out_ip);
    proto_bool (*resolve_verified)(const char *host, uint32_t *out_ip);
#if !PROTOCORE_HOT
    /// Host test hook: make @ref ResolverNs::resolve answer @c ip when @c ok, else fail.
    void (*test_set_resolve)(proto_bool ok, uint32_t ip);
#endif
} ResolverNs;

/** @brief The one symbol this module exports. */
extern const ResolverNs Resolver;

PROTO_END_DECLS

#endif // PC_NEED_DNS_RESOLVER
#endif // PROTOCORE_DNS_RESOLVER_H
