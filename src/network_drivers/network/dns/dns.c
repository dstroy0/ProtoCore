// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns.c
 * @brief The RESOLVER and the NAME SERVER, joined. See dns.h.
 *
 * Nothing runs here. The file holds the one table that names the two components RFC 1034 sec 2.4
 * defines as programs, so a caller reaches both through @ref Dns and neither component has to know
 * the other exists.
 */

#include "network_drivers/network/dns/dns.h"

#include "network_drivers/network/dns/dns_resolver.h"
#include "network_drivers/network/dns/dns_server.h"

/**
 * @brief The two components (RFC 1034 sec 2.4).
 *
 * @var DnsInternal::resolver  the RESOLVER: extracts information from name servers in response to
 *                             client requests (RFC 1034 sec 5)
 * @var DnsInternal::server    the NAME SERVER: holds the domain tree's structure and set
 *                             information (RFC 1034 sec 4)
 * @var DnsInternal::present   what the struct holds when both components are gated out: nothing
 *
 * Pointers rather than values because a table in one translation unit is not a constant expression
 * in another, so a by-value member could not be initialized from here. A struct with no members is
 * not valid C, so @ref DnsInternal::present stands in when both flags are off.
 */
struct DnsInternal
{
#if PROTOCORE_NEED_DNS_RESOLVER
    ResolverNs *resolver;
#endif
#if PROTOCORE_ENABLE_DNS_SERVER
    DnsServerNs *server;
#endif
#if !PROTOCORE_NEED_DNS_RESOLVER && !PROTOCORE_ENABLE_DNS_SERVER
    proto_bool present;
#endif
};

// Designated, so a member's position in the struct does not decide what it binds to.
static struct DnsInternal s_dns = {
#if PROTOCORE_NEED_DNS_RESOLVER
    .resolver = &Resolver,
#endif
#if PROTOCORE_ENABLE_DNS_SERVER
    .server = &DnsServer,
#endif
#if !PROTOCORE_NEED_DNS_RESOLVER && !PROTOCORE_ENABLE_DNS_SERVER
    .present = PROTO_FALSE,
#endif
};

DnsNs Dns = {.internal = &s_dns};
