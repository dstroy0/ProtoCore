// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file network.h
 * @brief Layer 3 (Network) - the internet layer: the modules it carries, and its bring-up call.
 *
 * RFC 1122 sec 3 "INTERNET LAYER PROTOCOLS" is what sits here: IP (RFC 791) for v4 and RFC 8200
 * for v6, with address assignment and ICMP beside them. The platform's TCP/IP stack holds the route
 * table and performs RFC 1122 sec 3.3.1 "Routing Outbound Datagrams" itself, the Local/Remote
 * Decision (sec 3.3.1.1) and Gateway Selection (sec 3.3.1.2) included, so ::NetworkNs::init runs no
 * routing of its own. The layer names the modules a caller reaches through it.
 *
 * The module exports one symbol, @ref network. Everything in network.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NETWORK_H
#define PROTOCORE_NETWORK_H

#include "network_drivers/network/dns/dns/dns.h"
#include "shared/ip/ip.h"

#include "protocore_config.h" // first: the feature flags the includes below are gated on
#if PROTOCORE_ENABLE_FORWARD
#include "network_drivers/network/forward/forward.h" // ForwardNs: carried below as network.forward
#endif

PROTOCORE_BEGIN_DECLS

/**
 * @brief The internet layer (RFC 1122 sec 3), and the modules it carries.
 *
 * A caller invokes a call through ::network, passing the handle's own @ref internal, and reaches a
 * carried module by its member: `network.ip->parse(network.ip->internal)`.
 *
 * @var NetworkNs::dns       name resolution: the RESOLVER and the NAME SERVER (RFC 1034 sec 2.4
 *                           "Elements of the DNS", the messages between them RFC 1035 sec 4)
 * @var NetworkNs::forward   the forwarding plane: the ingress ACL, the policy routes, the fan-out
 *                           (RFC 1812 sec 5.2.1 "Forwarding Algorithm")
 * @var NetworkNs::ip        an internet address as a value: parse, format, classify, compare
 *                           (RFC 791 sec 1.4 addressing, RFC 8200 for v6)
 * @var NetworkNs::init      bring the layer up. It runs no work: the platform stack holds the route
 *                           table and selects the path (RFC 791 sec 1.4 "Operation")
 *
 * Each carried module is named by pointer; one behind a feature flag is declared under that flag,
 * so the layer names only what the image already contains.
 *
 * No argument members: init reads nothing. No result members: init reports nothing.
 * No storage member: the layer holds nothing of its own, each carried module owns its state.
 */
typedef struct
{
#if PROTOCORE_ENABLE_DNS
    DnsNs *dns; ///< the RESOLVER and the NAME SERVER (RFC 1034 sec 2.4)
#endif
#if PROTOCORE_ENABLE_FORWARD
    ForwardNs *forward; ///< the forwarding plane (RFC 1812 sec 5)
#endif
    IpNs *ip; ///< an internet address as a value (RFC 791, RFC 8200)
} networkVars;

/** @brief The operands and the outcome. */
extern networkVars networkV;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
} NetworkNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in networkV or a region of the borrow at a fixed offset.
void protocore_network_init(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `network.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const NetworkNs network __attribute__((unused)) = {
    .init = protocore_network_init,
};

PROTOCORE_END_DECLS

#endif
