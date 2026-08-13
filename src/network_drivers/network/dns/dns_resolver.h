// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file resolver.h
 * @brief DNS resolver with answer verification (PROTOCORE_ENABLE_DNS_RESOLVER).
 *
 * Resolves a hostname to an IPv4 address and classifies / verifies the answer: a remote name
 * resolving to 0.0.0.0, the broadcast address, loopback, or a multicast address is rejected as a
 * spoof / DNS-rebinding indicator.
 *
 * Nothing here blocks. The module owns one timer and one in-flight query: ::ResolverNs::resolve
 * starts the query, marks itself busy and returns ::PROTOCORE_DNS_BUSY, and the caller asks again on its
 * own tick. The reply arrives on the UDP listener's normal drain, so the answer lands without this
 * module pumping anything. Busy is the sending side only - the reply handler is always armed.
 *
 * Two backends, chosen by PROTOCORE_HAS_VENDOR_DNS_RESOLVER. Where the stack has its own resolver the
 * module marshals into it and inherits its nameserver and its cache. Where it does not, the portable
 * resolver asks PROTOCORE_DNS_SERVER over the UDP listener - one query at a time, no cache - and
 * ::ResolverNs::set_server points it at whatever address DHCP or provisioning turned up.
 *
 * The query and the answer are codecs in their own right, so they are exported and tested as such:
 * ::protocore_dns_query_build writes the question, ::protocore_dns_answer_parse reads the first A record back, and
 * the resolve is what puts a socket between them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNS_RESOLVER_H
#define PROTOCORE_DNS_RESOLVER_H

#include "protocore_config.h"

#if PROTOCORE_NEED_DNS_RESOLVER

PROTOCORE_BEGIN_DECLS

/** @brief IPv4 address category (RFC special-purpose ranges). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_IP_UNSPECIFIED = 0, ///< 0.0.0.0
    PROTOCORE_IP_LOOPBACK,        ///< 127.0.0.0/8
    PROTOCORE_IP_PRIVATE,         ///< 10/8, 172.16/12, 192.168/16
    PROTOCORE_IP_LINKLOCAL,       ///< 169.254.0.0/16
    PROTOCORE_IP_MULTICAST,       ///< 224.0.0.0/4
    PROTOCORE_IP_BROADCAST,       ///< 255.255.255.255
    PROTOCORE_IP_PUBLIC,          ///< globally-routable unicast
} protocore_ip_class;

/** @brief Where a resolve stands. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_DNS_READY = 0, ///< out_ip holds the address
    PROTOCORE_DNS_BUSY,      ///< a query is out; ask again on the next tick
    PROTOCORE_DNS_FAILED,    ///< the query did not leave, or the one in flight passed its deadline
} protocore_dns_state;

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

/**
 * @brief Write a standard A-record question for @p host into @p out (RFC 1035 sec 4.1).
 *
 * Header with @p id and recursion desired, then one question: the name, QTYPE A, QCLASS IN.
 *
 * @return bytes written, or 0 when the name does not encode or does not fit @p cap.
 */
size_t protocore_dns_query_build(uint8_t *out, size_t cap, uint16_t id, const char *host);

/**
 * @brief Read the first A record out of a response into @p out_ip, host order.
 *
 * Refuses a response whose id is not @p id, that is not a response, that carries a nonzero RCODE, or
 * that holds no A record in class IN. Walks past CNAMEs and any other type rather than assuming the
 * first answer is the address, and follows the compression pointers those answers use.
 *
 * @return true when an address was found, false also when the module's storage is unavailable.
 */
proto_bool protocore_dns_answer_parse(const uint8_t *pkt, size_t len, uint16_t id, uint32_t *out_ip);

// ---------------------------------------------------------------------------
// Resolve
// ---------------------------------------------------------------------------

/**
 * @brief The DNS resolver.
 *
 * @var ResolverNs::classify  what kind of address a host-order IPv4 word names
 * @var ResolverNs::verify    whether that word is a plausible A-record answer for a remote host
 * @var ResolverNs::resolve   ask for a host's IPv4 address, host order
 * @var ResolverNs::resolve_verified  as resolve, and require the answer to pass @ref ResolverNs::verify
 * @var ResolverNs::busy      a query is out
 * @var ResolverNs::set_server  the nameserver the portable backend asks, as a literal address
 *
 * resolve() answers a dotted quad from the name itself and reports ::PROTOCORE_DNS_READY. Any other name
 * starts a query, marks the module busy and reports ::PROTOCORE_DNS_BUSY; the caller asks again with the
 * same host on its next tick and gets ::PROTOCORE_DNS_READY once the reply parses, or ::PROTOCORE_DNS_FAILED once
 * PROTOCORE_DNS_TIMEOUT_MS passes. One query is in flight at a time, so a second host asked while busy
 * reports ::PROTOCORE_DNS_BUSY until the first settles.
 *
 * No storage member: the timer and the in-flight query belong to dns_resolver.c, and a caller
 * reaches them by calling.
 */
typedef struct
{
    protocore_ip_class (*classify)(uint32_t ip);
    proto_bool (*verify)(uint32_t ip);
    protocore_dns_state (*resolve)(const char *host, uint32_t *out_ip);
    protocore_dns_state (*resolve_verified)(const char *host, uint32_t *out_ip);
    proto_bool (*busy)(void);
    /**
     * @brief Point the resolver at @p ip, a dotted quad, replacing PROTOCORE_DNS_SERVER.
     *
     * What DHCP or provisioning turned up, once the app has it. False when @p ip does not parse, and
     * the previous server stands. On the vendor backend the stack owns its own nameserver list, so
     * this reports false and changes nothing.
     */
    proto_bool (*set_server)(const char *ip);
} ResolverNs;

/** @brief The one symbol this module exports. */
extern const ResolverNs Resolver;

PROTOCORE_END_DECLS

#endif // PROTOCORE_NEED_DNS_RESOLVER
#endif // PROTOCORE_DNS_RESOLVER_H
