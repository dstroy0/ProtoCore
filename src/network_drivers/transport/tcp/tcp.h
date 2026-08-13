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

PROTOCORE_BEGIN_DECLS

/** @brief The three halves, held where only tcp.c describes them. */
struct TcpInternal;

/**
 * @brief The connection oriented transport.
 *
 * @var TcpNs::internal  the pool, the listener and the client, reached through tcp.c
 */
typedef struct
{
    struct TcpInternal *internal;
} TcpNs;

/** @brief The one symbol this module exports. */
extern TcpNs Tcp;

PROTOCORE_END_DECLS

#endif // PROTOCORE_TCP_H
