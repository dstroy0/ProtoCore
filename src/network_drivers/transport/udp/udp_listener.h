// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_listener.h
 * @brief Layer 4 UDP, the receiving side: bound ports, their rings, and the drain.
 *
 * One slot per bound port. Each slot owns a receive ring and a send ring.
 *
 * The stack's receive trampoline is the sole producer of the receive ring; poll() is its sole
 * consumer and calls the handler once per datagram. reply() and sendto() are the sole producers of
 * the send ring; poll() is its sole consumer and moves frames to the wire as the stack accepts
 * them. The handler therefore runs in the task that calls poll(), not in the stack's thread.
 *
 * Both rings carry framed entries: address, port, and length ahead of the payload. A frame is
 * published once, whole, so the other side never observes a partial one. A datagram that does not
 * fit the free space is dropped, at the trampoline on receive and by refusing the call on send.
 *
 * Reached as `Udp.listener->listen(...)`.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_LISTENER_H
#define PROTOCORE_UDP_LISTENER_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

/**
 * @brief The sender of a received datagram.
 *
 * Address and port by value, copied out of the ring frame by poll(), plus the slot the datagram
 * arrived on so a reply leaves from the same endpoint. Valid for the duration of the handler call.
 * The layout lives in udp_listener.c so no stack type escapes the transport.
 */
struct pc_udp_peer;

/**
 * @brief Datagram handler, invoked once per received datagram by poll().
 *
 * @param data  contiguous payload, staged out of the slot's receive ring.
 * @param len   payload length in bytes.
 * @param peer  reply token, valid only during this call.
 * @param ctx   the opaque context passed to listen().
 */
typedef void (*pc_udp_handler)(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx);

/**
 * @brief The receiving side of UDP.
 *
 * @var UdpListenerNs::listen            bind a port and route its datagrams to a handler
 * @var UdpListenerNs::listen_multicast  bind a port and join an IPv4 group on every interface
 * @var UdpListenerNs::leave_multicast   leave the group bound on a port and free its slot
 * @var UdpListenerNs::poll              deliver received frames to handlers, move sent frames to the wire
 * @var UdpListenerNs::reply             queue an answer to the peer a handler was given
 * @var UdpListenerNs::peer_addr         copy a peer's address and port out
 * @var UdpListenerNs::sendto            queue a datagram from a bound port to an arbitrary destination
 * @var UdpListenerNs::sndbuf            bytes a bound port's send ring can still take
 *
 * reply() and sendto() report that the datagram was queued, not that it reached the wire. They
 * return false when the ring cannot take it; the caller retries after the next poll(), or reads
 * sndbuf() first and paces.
 */
typedef struct
{
    proto_bool (*listen)(uint16_t port, pc_udp_handler handler, void *ctx);
    proto_bool (*listen_multicast)(const char *group_ip, uint16_t port, pc_udp_handler handler, void *ctx);
    proto_bool (*leave_multicast)(uint16_t port);
    void (*poll)(void);
    proto_bool (*reply)(const struct pc_udp_peer *peer, const uint8_t *data, size_t len);
    proto_bool (*peer_addr)(const struct pc_udp_peer *peer, char *ip_out, size_t ip_cap, uint16_t *port_out);
    proto_bool (*sendto)(uint16_t listen_port, const char *dst_ip, uint16_t dst_port, const uint8_t *data, size_t len);
    size_t (*sndbuf)(uint16_t listen_port);
#if !PROTOCORE_HOT
    // Host test seams. inject() frames a datagram into the addressed slot's receive ring, the same
    // producer the stack drives, and poll() delivers it. The capture holds the last datagram this
    // side sent; the client's sends go to the client's own capture.
    void (*inject)(uint16_t listen_port, const char *src_ip, uint16_t src_port, const uint8_t *data, size_t len);
    void (*reset)(void);
    const char *(*joined_group)(uint16_t port);
    void (*set_sendto_result)(proto_bool ok);
    void (*capture_enable)(void);
    void (*capture_reset)(void);
    const uint8_t *(*captured)(void);
    size_t (*captured_len)(void);
#endif
} UdpListenerNs;

/** @brief The one symbol this module exports. */
extern const UdpListenerNs UdpListener;

PROTO_END_DECLS

#endif // PROTOCORE_UDP_LISTENER_H
