// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_server.h
 * @brief Layer 3 name service - the answering side of DNS (RFC 1035), UDP port 53.
 *
 * A name server for a network with no path to a real one: register `name -> IPv4` A records
 * (RFC 1035 sec 3.4.1) with @ref DnsServerNs::add and the device answers a question whose
 * QTYPE is A and QCLASS is IN from that fixed table, and answers RCODE 3 Name Error for a name
 * the table does not hold (RFC 1035 sec 4.1.1). Devices then use `printer.lan` instead of
 * `192.168.1.5`, a companion to the NTP server for self-hosted, offline infrastructure. Zero
 * heap; gated by PROTOCORE_ENABLE_DNS_SERVER.
 *
 * @ref DnsServerNs::build_response is pure: it reads a query message and writes a response
 * message (RFC 1035 sec 4.1), asking a resolver callback for each QNAME, so the wire format is
 * host-tested with no network stack under it. @ref DnsServerNs::begin binds UDP port 53
 * (RFC 1035 sec 4.2.1) through the transport UDP listener and serves the built-in table
 * (@ref DnsServerNs::lookup). It is a general resolver, distinct from the provisioning
 * captive-portal DNS, which answers every name with the access point's own address; do not
 * enable both.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNS_SERVER_H
#define PROTOCORE_DNS_SERVER_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_INLINE

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_DNS_SERVER

/**
 * @brief Resolve a QNAME to an IPv4 ADDRESS (RFC 1035 sec 3.4.1).
 * @param name the queried name, upper or lower case as sent (match ignoring case, sec 2.3.3).
 * @return the address in host byte order (0xC0A80105 == 192.168.1.5), or 0 for "not found".
 */
typedef uint32_t (*DnsResolveFn)(const char *name);

/** @brief RFC 1035 sec 4.1: the query message that is read, and the response message written. */
typedef struct
{
    const uint8_t *query; ///< the received query message, header first (RFC 1035 sec 4.1.1)
    size_t qlen;          ///< its length in octets; under 12 is shorter than the header
    uint8_t *out;         ///< where the response message is written
    size_t out_cap;       ///< how many octets that holds
} DnsMsgArgs;

/** @brief RFC 1035 sec 4.1.3: what the answer resource record carries, and where its RDATA comes from. */
typedef struct
{
    uint32_t ttl;         ///< the TTL the answer RR advertises, in seconds (RFC 1035 sec 4.1.3)
    DnsResolveFn resolve; ///< what a QNAME is resolved through
} DnsAnswerArgs;

/** @brief One A record of the built-in table: an owner name and its ADDRESS (RFC 1035 sec 3.4.1). */
typedef struct
{
    const char *name; ///< the owner name, matched ignoring ASCII case (RFC 1035 sec 2.3.3)
    uint8_t a;        ///< ADDRESS octet 1 (RFC 1035 sec 3.4.1)
    uint8_t b;        ///< ADDRESS octet 2
    uint8_t c;        ///< ADDRESS octet 3
    uint8_t d;        ///< ADDRESS octet 4
} DnsRecordArgs;

/** @brief The name table's own state and the calls that reach it, described only in dns_server.c. */
struct DnsServerInternal;

/**
 * @brief The answering side of DNS.
 *
 * RFC 1035 sec 4.1: a query message in, a response message out. A caller sets the members a call
 * takes, invokes it through ::DnsServer, and reads the outcome off the same handle. The name
 * table itself is behind @ref internal.
 *
 * @var DnsServerNs::msg             the query message read and the response written (RFC 1035 sec 4.1)
 * @var DnsServerNs::ans             what the answer RR carries (RFC 1035 sec 4.1.3)
 * @var DnsServerNs::rec             the record a table call names (RFC 1035 sec 3.4.1)
 * @var DnsServerNs::ok              a call's true/false outcome
 * @var DnsServerNs::n               octets the response builder wrote, 0 on a malformed query or
 *                                   a response buffer too small
 * @var DnsServerNs::ip              the ADDRESS a lookup found, host order, 0 when the name is absent
 * @var DnsServerNs::build_response  frame a response to a query, asking @c ans.resolve for the QNAME
 * @var DnsServerNs::add             record one name and its A record
 * @var DnsServerNs::clear           forget every recorded name
 * @var DnsServerNs::begin           start answering on UDP port 53 (RFC 1035 sec 4.2.1)
 * @var DnsServerNs::lookup          the ADDRESS recorded for a name, or 0
 * @var DnsServerNs::internal        the name table and the calls that reach it
 */
typedef struct
{
    DnsMsgArgs msg;    ///< the message pair a response is built from (RFC 1035 sec 4.1)
    DnsAnswerArgs ans; ///< what the answer RR carries (RFC 1035 sec 4.1.3)
    DnsRecordArgs rec; ///< the record a table call names (RFC 1035 sec 3.4.1)

    proto_bool ok;
    size_t n;
    uint32_t ip;

    void (*build_response)(struct DnsServerInternal *ctx);
    void (*add)(struct DnsServerInternal *ctx);
    void (*clear)(struct DnsServerInternal *ctx);
    void (*begin)(struct DnsServerInternal *ctx);
    void (*lookup)(struct DnsServerInternal *ctx);

    struct DnsServerInternal *internal;
} DnsServerNs;

/** @brief The one symbol this module exports. */
extern DnsServerNs DnsServer;

/**
 * @brief The built-in table as a ::DnsResolveFn, for @ref DnsServerNs::ans.resolve.
 *
 * Scans the same records @ref DnsServerNs::add fills and @ref DnsServerNs::lookup reads, matching
 * ignoring ASCII case (RFC 1035 sec 2.3.3), and touches no member of ::DnsServer, so a build in
 * progress keeps the members it was given. @ref DnsServerNs::begin resolves through this.
 *
 * @return the ADDRESS in host byte order, or 0 when the name is absent.
 */
uint32_t protocore_dns_server_resolve(const char *name);

#endif // PROTOCORE_ENABLE_DNS_SERVER

PROTOCORE_END_DECLS

#endif // PROTOCORE_DNS_SERVER_H
