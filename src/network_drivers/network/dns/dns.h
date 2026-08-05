// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns.h
 * @brief Name resolution, both directions: asking and answering.
 *
 * One module with two halves. The resolver turns a name into an address; the server answers a query
 * with one. They are the same protocol seen from either end, so they sit together and a caller
 * reaches whichever it needs through @ref Dns rather than knowing two modules.
 *
 * Reached as `network.dns->resolver->resolve(...)`. The halves are pointers rather than values
 * because a table in one translation unit is not a constant expression in another, so a by-value
 * member could not be initialized from here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNS_H
#define PROTOCORE_DNS_H

#include "network_drivers/network/dns/resolver.h"
#include "network_drivers/network/dns/server.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

/**
 * @brief Name resolution.
 *
 * @var DnsNs::resolver  turning a name into an address
 * @var DnsNs::server    answering a query with one
 *
 * A half that its feature flag leaves out is a null member, so a caller tests the pointer rather
 * than repeating the flag.
 */
typedef struct
{
#if PC_NEED_DNS_RESOLVER
    const ResolverNs *resolver;
#endif
#if PC_ENABLE_DNS_SERVER
    const DnsServerNs *server;
#endif
    // A struct with no members is not valid C, so the module keeps one when both halves are gated
    // out. It states the same thing the members do: nothing here is available.
#if !PC_NEED_DNS_RESOLVER && !PC_ENABLE_DNS_SERVER
    proto_bool present;
#endif
} DnsNs;

/** @brief The one symbol this module exports. */
extern const DnsNs Dns;

PROTO_END_DECLS

#endif // PROTOCORE_DNS_H
