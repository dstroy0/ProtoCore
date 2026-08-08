// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_listener.h
 * @brief Layer 4 UDP, the receiving side: bound ports, their receive rings, and the drain.
 *
 * One slot per bound port, and one receive ring per slot. The stack's receive trampoline is its
 * sole producer; poll() is its sole consumer and calls the handler once per datagram, so the
 * handler runs in the task that calls poll(), not in the stack's thread.
 *
 * The ring carries framed entries: address, port, and length ahead of the payload. A frame is
 * published once, whole, so the consumer never observes a partial one, and a datagram that does not
 * fit the free space is dropped at the trampoline.
 *
 * Sending is not queued. reply() and sendto() carry the caller's buffer to the wire inside one
 * marshaled call and report what the stack did with it.
 *
 * Reached as `Udp.listener->listen(...)`.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_LISTENER_H
#define PROTOCORE_UDP_LISTENER_H

#include "protocore_config.h"
#include "shared_primitives/ip.h" // pc_ip: the destination, already an address

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
 * @var UdpListenerNs::poll              deliver received datagrams to their handlers
 * @var UdpListenerNs::reply             answer the peer a handler was given
 * @var UdpListenerNs::peer_addr         copy a peer's address and port out
 * @var UdpListenerNs::sendto            send from a bound port to an arbitrary destination
 * @var UdpListenerNs::close             unbind a port and free its slot, leaving any group first
 * @var UdpListenerNs::joined_group      the group a port joined, formatted, or NULL
 *
 * reply() and sendto() send the caller's bytes from where they already are, and report whether the
 * stack took them. There is nothing between the caller and the wire: a refusal means the datagram
 * did not leave, the caller's buffer is untouched, and the caller sends it again. A datagram longer
 * than ::PC_UDP_RX_BUF_SIZE is refused.
 */
typedef struct
{
    proto_bool (*listen)(uint16_t port, pc_udp_handler handler, void *ctx);
    proto_bool (*listen_multicast)(const char *group_ip, uint16_t port, pc_udp_handler handler, void *ctx);
    proto_bool (*leave_multicast)(uint16_t port);
    void (*poll)(void);
    proto_bool (*reply)(const struct pc_udp_peer *peer, const uint8_t *data, size_t len);
    proto_bool (*peer_addr)(const struct pc_udp_peer *peer, char *ip_out, size_t ip_cap, uint16_t *port_out);
    proto_bool (*sendto)(uint16_t listen_port, const pc_ip *dst, uint16_t dst_port, const uint8_t *data, size_t len);
    proto_bool (*close)(uint16_t port);
    const char *(*joined_group)(uint16_t port);
} UdpListenerNs;

/** @brief The one symbol this module exports. */
extern const UdpListenerNs UdpListener;

PROTO_END_DECLS

#endif // PROTOCORE_UDP_LISTENER_H
