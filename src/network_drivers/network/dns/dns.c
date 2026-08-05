// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns.c
 * @brief The two halves of name resolution, joined. See dns.h.
 *
 * Nothing runs here. The file exists to hold the one table that names the resolver and the server,
 * so a caller reaches both through @ref Dns and neither half has to know the other exists.
 */

#include "network_drivers/network/dns/dns.h"

const DnsNs Dns = {
#if PC_NEED_DNS_RESOLVER
    &Resolver,
#endif
#if PC_ENABLE_DNS_SERVER
    &DnsServer,
#endif
#if !PC_NEED_DNS_RESOLVER && !PC_ENABLE_DNS_SERVER
    PROTO_FALSE,
#endif
};
