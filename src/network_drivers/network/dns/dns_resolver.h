// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_resolver.h
 * @brief Layer 3 (Network) - the asking side of DNS: a question out, an A record back
 *        (PROTOCORE_ENABLE_DNS_RESOLVER).
 *
 * RFC 1035 sec 4.1.2 QNAME in, RFC 1035 sec 3.4.1 A RDATA out. The query carries one question,
 * QTYPE A, QCLASS IN, with RD set (RFC 1035 sec 4.1.1), and travels over UDP to server port 53
 * (RFC 1035 sec 4.2.1).
 *
 * A response is accepted only when its ID echoes the query's, QR is set, and RCODE is 0
 * (RFC 1035 sec 4.1.1); RFC 5452 sec 9.1 names the ID as one of the attributes a response has to
 * match before its data is used. The address it yields is then classified against the IPv4
 * Special-Purpose Address Registry (RFC 6890 sec 2.2.2) and the host group range
 * (RFC 1112 sec 4): a remote name answering with "This host on this network", Limited Broadcast,
 * Loopback, or a host group address is refused.
 *
 * Nothing here blocks. The module owns one timer and one in-flight query: ::ResolverNs::resolve
 * starts the query, marks itself busy and reports ::PROTOCORE_DNS_BUSY, and the caller asks again on
 * its own tick. The response arrives on the UDP listener's normal drain, so the answer lands without
 * this module pumping anything. Busy is the sending side only, and the response handler is always
 * armed.
 *
 * Two backends, chosen by PROTOCORE_HAS_VENDOR_DNS_RESOLVER. Where the platform's stack has its own
 * resolver the module marshals into it and inherits its nameserver list and its cache. Where it does
 * not, the portable resolver asks PROTOCORE_DNS_SERVER over the UDP listener, one query at a time
 * with no cache, and ::ResolverNs::set_server points it at whatever address DHCP or provisioning
 * turned up.
 *
 * The query and the response are codecs in their own right, so they are reached as calls and tested
 * as such: ::ResolverNs::query_build writes the question section, ::ResolverNs::answer_parse reads
 * the first A record back, and the resolve is what puts a socket between them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNS_RESOLVER_H
#define PROTOCORE_DNS_RESOLVER_H

#include "protocore_config.h"

#if PROTOCORE_NEED_DNS_RESOLVER

PROTOCORE_BEGIN_DECLS

/**
 * @brief RFC 1034 sec 5.3.1: what the platform's resolver hands back when an answer arrives.
 *
 * The name is echoed so one callback can serve several outstanding queries; @p addr is null when
 * the name did not resolve.
 */
typedef void (*protocore_net_dns_found_fn)(const char *name, const protocore_net_ip *addr, void *arg);

/** @brief IPv4 address category: RFC 6890 sec 2.2.2 registry entries, plus RFC 1112 sec 4. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_IP_UNSPECIFIED = 0, ///< 0.0.0.0/8 "This host on this network" (RFC 6890 sec 2.2.2)
    PROTOCORE_IP_LOOPBACK,        ///< 127.0.0.0/8 Loopback (RFC 6890 sec 2.2.2)
    PROTOCORE_IP_PRIVATE,         ///< 10/8, 172.16/12, 192.168/16 Private-Use (RFC 6890 sec 2.2.2)
    PROTOCORE_IP_LINKLOCAL,       ///< 169.254.0.0/16 Link Local (RFC 6890 sec 2.2.2)
    PROTOCORE_IP_MULTICAST,       ///< 224.0.0.0 to 239.255.255.255 host group (RFC 1112 sec 4)
    PROTOCORE_IP_BROADCAST,       ///< 255.255.255.255/32 Limited Broadcast (RFC 6890 sec 2.2.2)
    PROTOCORE_IP_PUBLIC,          ///< globally-routable unicast
} protocore_ip_class;

/** @brief Where a resolve stands. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_DNS_READY = 0, ///< u32 holds the address
    PROTOCORE_DNS_BUSY,      ///< a query is out; ask again on the next tick
    PROTOCORE_DNS_FAILED,    ///< the query did not leave, or the one in flight passed its deadline
} protocore_dns_state;

/** @brief The address a classify or a verify judges, as a host-order word. */
typedef struct
{
    uint32_t ip; ///< the IPv4 address, host order (e.g. (10u << 24) | (0u << 16) | (0u << 8) | 1u)
} DnsAddrArgs;

/** @brief RFC 1035 sec 4.1.2 question section: the name asked for, its ID, and where it is written. */
typedef struct
{
    const char *host; ///< QNAME, dotted (RFC 1035 sec 4.1.2)
    uint16_t id;      ///< the header ID a build stamps and a parse demands (RFC 1035 sec 4.1.1)
    uint8_t *out;     ///< where a build writes the query message
    size_t cap;       ///< how many octets that has
} DnsQueryArgs;

/** @brief RFC 1035 sec 4.1.3 answer section: the response message a parse reads. */
typedef struct
{
    const uint8_t *pkt; ///< the response message as it arrived
    size_t len;         ///< its length in octets
} DnsAnswerArgs;

/** @brief The nameserver the portable backend sends its queries to (RFC 1035 sec 4.2.1). */
typedef struct
{
    const char *ip; ///< its address as a dotted quad
} DnsServerArgs;

/** @brief The resolver's own state and the calls that reach it, described only in dns_resolver.c. */
struct ResolverInternal;

/**
 * @brief The DNS resolver.
 *
 * A caller sets the members a call takes, invokes it through ::Resolver, and reads the outcome off
 * the same handle. The in-flight query and its timer are behind @ref internal.
 *
 * @var ResolverNs::addr    the address a classify or a verify judges
 * @var ResolverNs::query   the question a resolve or a build asks (RFC 1035 sec 4.1.2)
 * @var ResolverNs::answer  the response message a parse reads (RFC 1035 sec 4.1.3)
 * @var ResolverNs::server  the nameserver the portable backend asks (RFC 1035 sec 4.2.1)
 * @var ResolverNs::ok      a call's true/false outcome
 * @var ResolverNs::n       octets a build wrote, or 0 when the name does not encode or does not fit
 * @var ResolverNs::u32     the address a resolve or a parse reports, host order; 0 when it has none
 * @var ResolverNs::cls     the category a classify reports
 * @var ResolverNs::state   where a resolve stands
 * @var ResolverNs::classify      what kind of address a host-order IPv4 word names
 * @var ResolverNs::verify        whether that word is a plausible A record for a remote host
 * @var ResolverNs::query_build   write a standard A-record question (RFC 1035 sec 4.1.1, sec 4.1.2)
 * @var ResolverNs::answer_parse  read the first A record out of a response (RFC 1035 sec 3.4.1)
 * @var ResolverNs::resolve       ask for a host's IPv4 address, host order
 * @var ResolverNs::resolve_verified  as resolve, and require the answer to pass @ref verify
 * @var ResolverNs::busy      a query is out
 * @var ResolverNs::set_server  point the portable backend at a nameserver, as a literal address
 * @var ResolverNs::internal  the in-flight query, its timer, and the calls that reach them
 *
 * resolve answers a dotted quad from the name itself and reports ::PROTOCORE_DNS_READY. Any other
 * name starts a query, marks the module busy and reports ::PROTOCORE_DNS_BUSY; the caller asks again
 * with the same host on its next tick and gets ::PROTOCORE_DNS_READY once the response parses, or
 * ::PROTOCORE_DNS_FAILED once PROTOCORE_DNS_TIMEOUT_MS passes. One query is in flight at a time, so
 * a second host asked while busy reports ::PROTOCORE_DNS_BUSY until the first settles.
 *
 * query_build writes a header carrying @ref DnsQueryArgs::id with RD set and QDCOUNT 1, then one
 * question: the name, QTYPE A, QCLASS IN.
 *
 * answer_parse refuses a response whose ID is not @ref DnsQueryArgs::id, that is not a response,
 * that carries a nonzero RCODE, or that holds no A record in class IN (RFC 1035 sec 4.1.1,
 * RFC 5452 sec 9.1). It walks past CNAMEs and any other TYPE rather than assuming the first answer
 * is the address, and follows the compression pointers those names use (RFC 1035 sec 4.1.4). It
 * also reports false when the module's storage is unavailable.
 *
 * set_server replaces PROTOCORE_DNS_SERVER with what DHCP or provisioning turned up, once the app
 * has it. It reports false when the address does not parse, and the previous server stands. On the
 * vendor backend the stack owns its own nameserver list, so it reports false and changes nothing.
 */
typedef struct
{
    DnsAddrArgs addr;     ///< what a classify or a verify judges (RFC 6890 sec 2.2.2)
    DnsQueryArgs query;   ///< what a question names (RFC 1035 sec 4.1.2)
    DnsAnswerArgs answer; ///< what a parse reads (RFC 1035 sec 4.1.3)
    DnsServerArgs server; ///< where a portable query is sent (RFC 1035 sec 4.2.1)

    proto_bool ok;
    size_t n;
    uint32_t u32;
    protocore_ip_class cls;
    protocore_dns_state state;

    void (*classify)(struct ResolverInternal *ctx);
    void (*verify)(struct ResolverInternal *ctx);
    void (*query_build)(struct ResolverInternal *ctx);
    void (*answer_parse)(struct ResolverInternal *ctx);
    void (*resolve)(struct ResolverInternal *ctx);
    void (*resolve_verified)(struct ResolverInternal *ctx);
    void (*busy)(struct ResolverInternal *ctx);
    void (*set_server)(struct ResolverInternal *ctx);

    struct ResolverInternal *internal;
} ResolverNs;

/** @brief The one symbol this module exports. */
extern ResolverNs Resolver;

PROTOCORE_END_DECLS

#endif // PROTOCORE_NEED_DNS_RESOLVER
#endif // PROTOCORE_DNS_RESOLVER_H
