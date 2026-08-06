// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp.h
 * @brief Layer 4 (Transport), connectionless: the two sides of UDP, joined.
 *
 * One module with two halves. The listener binds a port, receives on it, and answers from it; the
 * client sends from an ephemeral port and receives nothing. They are the same datagram service seen
 * from either end, so they sit together and a caller reaches whichever it needs through @ref Udp
 * rather than knowing two modules.
 *
 * Both halves own their stack plumbing (`udp_pcb`, `pbuf`) and hand out framed rings instead, so no
 * stack type reaches a layer above this one.
 *
 * Reached as `Udp.listener->listen(...)` and `Udp.client->sendto(...)`. The halves are pointers
 * rather than values because a table in one translation unit is not a constant expression in
 * another, so a by-value member could not be initialized from here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_H
#define PROTOCORE_UDP_H

#include "network_drivers/transport/udp/udp_client.h"
#include "network_drivers/transport/udp/udp_listener.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

/**
 * @brief The datagram service.
 *
 * @var UdpNs::listener  bound ports: receiving, replying, and sending from the bound endpoint
 * @var UdpNs::client    an ephemeral source port: sending only
 */
typedef struct
{
    const UdpListenerNs *listener;
    const UdpClientNs *client;
} UdpNs;

/** @brief The one symbol this module exports. */
extern const UdpNs Udp;

PROTO_END_DECLS

#endif // PROTOCORE_UDP_H
