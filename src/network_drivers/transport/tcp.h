// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tcp.h
 * @brief Layer 4 (Transport), connection oriented: the three sides of TCP, joined.
 *
 * One module with three halves, because a connection is reached three ways. The listener binds a
 * port and accepts on it, the pool holds what was accepted, and the client dials out. All three
 * speak to the same stack and hand out slot indices and rings, so no stack type reaches a layer
 * above this one.
 *
 * Reached as `Tcp.listener->stop(...)`, `Tcp.conn->send(...)` and `Tcp.client->open(...)`. The
 * halves are pointers rather than values because a table in one translation unit is not a constant
 * expression in another, so a by-value member could not be initialized from here.
 *
 * The per-slot ring accessors stay inline in tcp/tcp_conn.h rather than becoming members. A member
 * is an indirect call through rodata; those accessors are a load and a compare on the request path.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_H
#define PROTOCORE_TCP_H

#include "network_drivers/transport/tcp/tcp_client.h"
#include "network_drivers/transport/tcp/tcp_conn.h"
#include "network_drivers/transport/tcp/tcp_listener.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

/**
 * @brief The connection oriented transport.
 *
 * @var TcpNs::conn      the pool of accepted connections
 * @var TcpNs::listener  bound ports, their worker queues, and the accept-time gates
 * @var TcpNs::client    dialing out; present only when a client transport is enabled
 */
typedef struct
{
    const ConnPoolNs *conn;
    const TcpListenerNs *listener;
#if PC_NEED_CLIENT
    const TcpClientNs *client;
#endif
} TcpNs;

/** @brief The one symbol this module exports. */
extern const TcpNs Tcp;

PROTO_END_DECLS

#endif // PROTOCORE_TCP_H
