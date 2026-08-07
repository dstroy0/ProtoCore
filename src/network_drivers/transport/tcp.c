// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tcp.c
 * @brief The three sides of TCP, joined. See tcp.h.
 *
 * Nothing runs here. The file exists to hold the one table that names the pool, the listener and
 * the client, so a caller reaches all three through @ref Tcp and no half has to know the others
 * exist.
 */

#include "network_drivers/transport/tcp.h"

// Designated, so a member's position in the struct does not decide what it binds to.
const TcpNs Tcp = {
    .conn = &ConnPool,
    .listener = &TcpListener,
#if PC_NEED_CLIENT
    .client = &TcpClient,
#endif
};
