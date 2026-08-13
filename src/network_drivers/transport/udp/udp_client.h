// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_client.h
 * @brief Layer 4 UDP, the sending side: datagrams to an arbitrary destination.
 *
 * One shared outbound control block, created on first send.
 *
 * sendto() carries the caller's buffer to the stack's thread inside one marshaled call and reports
 * what the stack did with it. Nothing is queued and nothing is copied on the way out.
 *
 * Nothing is bound here, so the source port is ephemeral and nothing is received. A service whose
 * peer replies to the source endpoint binds a port on the listener side and sends with
 * `Udp.listener->sendto()` instead.
 *
 * Reached as `Udp.client->sendto(...)`.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_CLIENT_H
#define PROTOCORE_UDP_CLIENT_H

#include "protocore_config.h"
#include "shared_primitives/ip.h" // protocore_ip: the destination, already an address

PROTOCORE_BEGIN_DECLS

/**
 * @brief The sending side of UDP.
 *
 * @var UdpClientNs::sendto  send a datagram to an address and port
 *
 * sendto() sends the caller's bytes from where they already are, and reports whether the stack took
 * them. There is nothing between the caller and the wire: a refusal means the datagram did not
 * leave, the caller's buffer is untouched, and the caller sends it again. Nothing is queued, so
 * there is no drain to poll and no room to read.
 */
typedef struct
{
    proto_bool (*sendto)(const protocore_ip *dst, uint16_t dst_port, const uint8_t *data, size_t len);
} UdpClientNs;

/** @brief The one symbol this module exports. */
extern const UdpClientNs UdpClient;

PROTOCORE_END_DECLS

#endif // PROTOCORE_UDP_CLIENT_H
