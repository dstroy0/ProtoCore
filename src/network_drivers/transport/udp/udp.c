// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp.c
 * @brief The two sides of UDP, joined. See udp.h.
 *
 * Nothing runs here. The file exists to hold the one table that names the listener and the client,
 * so a caller reaches both through @ref Udp and neither half has to know the other exists.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_UDP

#include "network_drivers/transport/udp/udp.h"

#include "network_drivers/transport/udp/client/client.h"
#include "network_drivers/transport/udp/server/server.h"

PROTOCORE_BEGIN_DECLS

// RFC 768 gives the datagram a source port and a destination port; a bound port that receives is
// the listener, and sending to a destination is the client. Designated, so a member's position in
// the struct does not decide what it binds to.
UdpNs Udp = {.listener = &UdpListener, .client = &UdpClient};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_UDP
