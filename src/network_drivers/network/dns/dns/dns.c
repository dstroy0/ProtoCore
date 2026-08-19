// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns.c
 * @brief The RESOLVER and the NAME SERVER, joined. See dns.h.
 *
 * Nothing runs here. The file holds the one table that names the two components RFC 1034 sec 2.4
 * defines as programs, so a caller reaches both through @ref Dns and neither component has to know
 * the other exists.
 */

#include "network_drivers/network/dns/dns/dns.h"

// The two components RFC 1034 sec 2.4 defines as programs. Designated, so a member's position in
// the struct does not decide what it binds to.
DnsNs Dns = {
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
