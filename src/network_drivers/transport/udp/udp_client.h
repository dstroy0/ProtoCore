// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_client.h
 * @brief Layer 4 UDP, the sending side: datagrams to an arbitrary destination.
 *
 * One send ring and one shared outbound control block, created on first use.
 *
 * sendto() is the sole producer of the ring and frames the destination address, port, and length
 * ahead of the payload. poll() is its sole consumer and moves frames to the wire as the stack
 * accepts them. A frame is published once, whole; one that does not fit the free space is refused.
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
#include "shared_primitives/ip.h" // pc_ip: the destination, already an address

PROTO_BEGIN_DECLS

/**
 * @brief The sending side of UDP.
 *
 * @var UdpClientNs::sendto  queue a datagram to an address and port
 * @var UdpClientNs::poll    move queued frames to the wire
 * @var UdpClientNs::sndbuf  bytes the send ring can still take
 *
 * sendto() reports that the datagram was queued, not that it reached the wire. It returns false
 * when the ring cannot take it; the caller retries after the next poll(), or reads sndbuf() first
 * and paces.
 */
typedef struct
{
    proto_bool (*sendto)(const pc_ip *dst, uint16_t dst_port, const uint8_t *data, size_t len);
    void (*poll)(void);
    size_t (*sndbuf)(void);
#if !PROTOCORE_HOT
    // Host test seam: the last datagram this side sent. The listener's replies go to the listener's
    // own capture, so a test reads the side it drove.
    void (*capture_enable)(void);
    void (*capture_reset)(void);
    const uint8_t *(*captured)(void);
    size_t (*captured_len)(void);
#endif
} UdpClientNs;

/** @brief The one symbol this module exports. */
extern const UdpClientNs UdpClient;

PROTO_END_DECLS

#endif // PROTOCORE_UDP_CLIENT_H
