// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * Each half is reached through its own handle: ::ConnPool, ::TcpListener, ::TcpClient. The
 * halves are pointers rather than values because a table in one translation unit is not a constant
 * expression in another, so a by-value member could not be initialized from here.
 *
 * The per-slot ring accessors stay inline in common.h rather than becoming members. A member
 * is an indirect call through rodata; those accessors are a load and a compare on the request path.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_H
#define PROTOCORE_TCP_H

#include "protocore_config.h"

#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPoolNs: the accepted connections
#include "network_drivers/transport/tcp/server/server.h"     // TcpListenerNs: the bound ports
#if PROTOCORE_NEED_CLIENT
#include "network_drivers/transport/tcp/client/client.h" // TcpClientNs: dialing out
#endif

PROTOCORE_BEGIN_DECLS

/**
 * @brief The connection oriented transport.
 *
 * @var TcpNs::conn      the pool of accepted connections
 * @var TcpNs::listener  bound ports, their worker queues, and the accept-time gates
 * @var TcpNs::client    dialing out; present only when a client transport is enabled
 *
 * Pointers rather than values because a table in one translation unit is not a constant expression
 * in another, so a by-value member could not be initialized from tcp.c.
 */
typedef struct
{
    ConnPoolNs *const conn;
    TcpListenerNs *const listener;
#if PROTOCORE_NEED_CLIENT
    TcpClientNs *const client;
#endif
} TcpNs;

/** @brief The one symbol this module exports. */
extern TcpNs Tcp;

PROTOCORE_END_DECLS

#endif // PROTOCORE_TCP_H
