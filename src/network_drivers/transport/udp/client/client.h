// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file client.h
 * @brief Layer 4 UDP, the sending side: datagrams to an arbitrary destination.
 *
 * One shared outbound control block, created on first send.
 *
 * sendto() carries the caller's buffer to the stack's thread inside one marshaled call and reports
 * what the stack did with it. Nothing is queued and nothing is copied on the way out.
 *
 * Nothing is bound here, so the source port is ephemeral and nothing is received. A service whose
 * peer replies to the source endpoint binds a port on the listener side and sends with
 * ::UdpListener sendto instead.
 *
 * Reached through ::UdpClient.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_CLIENT_H
#define PROTOCORE_UDP_CLIENT_H

#include "shared/ip/ip.h" // protocore_ip: the destination, already an address

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief The sending side of UDP.
 *
 * RFC 768 "User Interface": an operation that allows a datagram to be sent, specifying the data and
 * the destination port and address. A caller sets the members the call takes, invokes it through
 * ::UdpClient, and reads the outcome off the same handle.
 *
 * @var UdpClientNs::dst       where the datagram goes
 * @var UdpClientNs::dst_port  its port
 * @var UdpClientNs::data      the octets to send
 * @var UdpClientNs::len       how many
 * @var UdpClientNs::ok        whether the stack took them
 * @var UdpClientNs::sendto    send a datagram to an address and port
 *
 * sendto() sends the caller's bytes from where they already are, and reports whether the stack took
 * them. There is nothing between the caller and the wire: a refusal means the datagram did not
 * leave, the caller's buffer is untouched, and the caller sends it again. Nothing is queued, so
 * there is no drain to poll and no room to read.
 */
typedef struct
{
    const protocore_ip *dst;
    uint16_t dst_port;
    const uint8_t *data;
    size_t len;

    proto_bool ok;

    void (*const sendto)(uint8_t *restrict work);
} UdpClientNs;

/** @brief The one symbol this module exports. */
extern UdpClientNs UdpClient;

/**
 * @brief The PROTOCORE_UDP_CLIENT_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_udp_client_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_UDP_CLIENT_H
