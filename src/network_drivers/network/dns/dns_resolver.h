// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file resolver.h
 * @brief DNS resolver with answer verification (PC_ENABLE_DNS_RESOLVER).
 *
 * Resolves a hostname to an IPv4 address and classifies / verifies the answer: a remote name
 * resolving to 0.0.0.0, the broadcast address, loopback, or a multicast address is rejected as a
 * spoof / DNS-rebinding indicator. The resolve is blocking (call it off the request hot path).
 *
 * Two backends, chosen by PC_HAS_VENDOR_DNS_RESOLVER. Where the stack has its own resolver the
 * module marshals into it and inherits its nameserver and its cache. Where it does not, the portable
 * resolver asks PC_DNS_SERVER over the UDP listener - one query per call, no cache - and
 * ::ResolverNs::set_server points it at whatever address DHCP or provisioning turned up.
 *
 * The query and the answer are codecs in their own right, so they are exported and tested as such:
 * ::pc_dns_query_build writes the question, ::pc_dns_answer_parse reads the first A record back, and
 * the blocking resolve is what puts a socket between them.
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

/**
 * @brief Write a standard A-record question for @p host into @p out (RFC 1035 sec 4.1).
 *
 * Header with @p id and recursion desired, then one question: the name, QTYPE A, QCLASS IN.
 *
 * @return bytes written, or 0 when the name does not encode or does not fit @p cap.
 */
size_t pc_dns_query_build(uint8_t *out, size_t cap, uint16_t id, const char *host);

/**
 * @brief Read the first A record out of a response into @p out_ip, host order.
 *
 * Refuses a response whose id is not @p id, that is not a response, that carries a nonzero RCODE, or
 * that holds no A record in class IN. Walks past CNAMEs and any other type rather than assuming the
 * first answer is the address, and follows the compression pointers those answers use.
 *
 * @return true when an address was found, false also when the module's storage is unavailable.
 */
proto_bool pc_dns_answer_parse(const uint8_t *pkt, size_t len, uint16_t id, uint32_t *out_ip);

// ---------------------------------------------------------------------------
// Resolve
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

/**
 * @brief The DNS resolver.
 *
 * @var ResolverNs::classify  what kind of address a host-order IPv4 word names
 * @var ResolverNs::verify    whether that word is a plausible A-record answer for a remote host
 * @var ResolverNs::resolve   resolve a host to an IPv4 address, host order; blocking
 * @var ResolverNs::resolve_verified  resolve, and require the answer to pass @ref ResolverNs::verify
 * @var ResolverNs::set_server  the nameserver the portable backend asks, as a literal address
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
    /**
     * @brief Point the resolver at @p ip, a dotted quad, replacing PC_DNS_SERVER.
     *
     * What DHCP or provisioning turned up, once the app has it. False when @p ip does not parse, and
     * the previous server stands. On the vendor backend the stack owns its own nameserver list, so
     * this reports false and changes nothing.
     */
    proto_bool (*set_server)(const char *ip);
} ResolverNs;

/** @brief The one symbol this module exports. */
extern const ResolverNs Resolver;

PROTO_END_DECLS

#endif // PC_NEED_DNS_RESOLVER
#endif // PROTOCORE_DNS_RESOLVER_H
