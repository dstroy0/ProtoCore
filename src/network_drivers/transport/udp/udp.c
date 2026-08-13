// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp.c
 * @brief The two sides of UDP, joined. See udp.h.
 *
 * Nothing runs here. The file exists to hold the one table that names the listener and the client,
 * so a caller reaches both through @ref Udp and neither half has to know the other exists.
 */

#include "network_drivers/transport/udp/udp.h"

#include "network_drivers/transport/udp/client/client.h"
#include "network_drivers/transport/udp/server/server.h"

/**
 * @brief The two sides of UDP.
 *
 * RFC 768 gives the datagram a source port and a destination port; a bound port that receives is
 * the listener, and sending to a destination is the client.
 *
 * @var UdpInternal::listener  a bound port and what arrives on it
 * @var UdpInternal::client    sending a datagram to a destination
 */
struct UdpInternal
{
    UdpListenerNs *listener;
    UdpClientNs *client;
};

// Designated, so a member's position in the struct does not decide what it binds to.
static struct UdpInternal s_udp = {.listener = &UdpListener, .client = &UdpClient};

UdpNs Udp = {.internal = &s_udp};
