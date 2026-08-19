// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns.h
 * @brief Layer 3 (Network) - name resolution, both directions: asking and answering.
 *
 * RFC 1034 sec 2.4 names two of the DNS's three major components as programs: RESOLVERS, "programs
 * that extract information from name servers in response to client requests", described in sec 5,
 * and NAME SERVERS, "server programs which hold information about the domain tree's structure and
 * set information", described in sec 4. This module holds one of each and nothing else.
 *
 * Each component is reached through its own handle: ::Resolver and ::DnsServer. Behind @ref
 * DnsNs::internal they are pointers rather than values, because a table in one translation unit is
 * not a constant expression in another, so a by-value member could not be initialized from here.
 *
 * A component its feature flag leaves out is not a member at all, so dns.c names only what the
 * image already contains.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNS_H
#define PROTOCORE_DNS_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DNS

#if PROTOCORE_NEED_DNS_RESOLVER
#include "network_drivers/network/dns/dns_resolver/dns_resolver.h" // ResolverNs: the RESOLVER (RFC 1034 sec 5)
#endif
#if PROTOCORE_ENABLE_DNS_SERVER
#include "network_drivers/network/dns/dns_server/dns_server.h" // DnsServerNs: the NAME SERVER (RFC 1034 sec 4)
#endif
PROTOCORE_BEGIN_DECLS

/**
 * @brief Name resolution (RFC 1034 sec 2.4).
 *
 * The handle carries no arguments and no results: it takes no call of its own, and each component
 * takes its own arguments off its own handle.
 *
 * No storage member: the RESOLVER's timer and in-flight query belong to dns_resolver.c and the NAME
 * SERVER's record table to dns_server.c, so this module owns no pool.
 *
 * @var DnsNs::resolver  extracts information from name servers in response to client requests
 *                       (RFC 1034 sec 5)
 * @var DnsNs::server    holds the domain tree's structure and set information (RFC 1034 sec 4)
 * @var DnsNs::present   what the table holds when both components are gated out: nothing
 *
 * Pointers rather than values because a table in one translation unit is not a constant expression
 * in another, so a by-value member could not be initialized from dns.c. A struct with no members is
 * not valid C, so @ref DnsNs::present stands in when both flags are off.
 */
typedef struct
{
#if PROTOCORE_NEED_DNS_RESOLVER
    ResolverNs *const resolver;
#endif
#if PROTOCORE_ENABLE_DNS_SERVER
    DnsServerNs *const server;
#endif
#if !PROTOCORE_NEED_DNS_RESOLVER && !PROTOCORE_ENABLE_DNS_SERVER
    proto_bool present;
#endif
} DnsNs;

/** @brief The one symbol this module exports. */
extern DnsNs Dns;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DNS

#endif // PROTOCORE_DNS_H
