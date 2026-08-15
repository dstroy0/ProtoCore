// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tcp.c
 * @brief The three sides of TCP, joined. See tcp.h.
 *
 * Nothing runs here. The file exists to hold the one table that names the pool, the listener and
 * the client, so a caller reaches all three through @ref Tcp and no half has to know the others
 * exist.
 */

#include "network_drivers/transport/tcp/tcp.h"

#include "network_drivers/transport/tcp/client/client.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"

/**
 * @brief The three halves.
 *
 * @var TcpInternal::conn      the pool of accepted connections
 * @var TcpInternal::listener  bound ports, their worker queues, and the accept-time gates
 * @var TcpInternal::client    dialing out; present only when a client transport is enabled
 *
 * Pointers rather than values because a table in one translation unit is not a constant expression
 * in another, so a by-value member could not be initialized from here.
 */
struct TcpInternal
{
    ConnPoolNs *conn;
    TcpListenerNs *listener;
#if PROTOCORE_NEED_CLIENT
    TcpClientNs *client;
#endif
};

// Designated, so a member's position in the struct does not decide what it binds to.
static struct TcpInternal s_tcp = {
    .conn = &ConnPool,
    .listener = &TcpListener,
#if PROTOCORE_NEED_CLIENT
    .client = &TcpClient,
#endif
};

TcpNs Tcp = {.internal = &s_tcp};
