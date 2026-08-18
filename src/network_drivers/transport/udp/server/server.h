// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file server.h
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
 * Reached through ::UdpListener.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_SERVER_H
#define PROTOCORE_UDP_SERVER_H

#include "protocore_config.h"
#include "shared/ip/ip.h" // protocore_ip: the destination, already an address

PROTOCORE_BEGIN_DECLS

/**
 * @brief The sender of a received datagram.
 *
 * Address and port by value, copied out of the ring frame by poll(), plus the slot the datagram
 * arrived on so a reply leaves from the same endpoint. Valid for the duration of the handler call.
 * The layout lives in server.c so no stack type escapes the transport.
 */
struct protocore_udp_peer;

/**
 * @brief Datagram handler, invoked once per received datagram by poll().
 *
 * @param data  contiguous payload, staged out of the slot's receive ring.
 * @param len   payload length in bytes.
 * @param peer  reply token, valid only during this call.
 * @param ctx   the opaque context passed to listen().
 */
typedef void (*protocore_udp_handler)(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer,
                                      void *ctx);

/** @brief RFC 768 "the creation of new receive ports": what binding one takes. */
typedef struct
{
    protocore_udp_handler handler; ///< what a received datagram is delivered to
    void *handler_ctx;             ///< the opaque context that handler is given back
    const char *group_ip;          ///< the IPv4 group to join, as text
} UdpBindArgs;

/** @brief RFC 768 "an operation that allows a datagram to be sent": where it goes and what it carries. */
typedef struct
{
    const protocore_ip *dst; ///< where a send goes
    uint16_t dst_port;       ///< its port
    const uint8_t *data;     ///< the octets to send
    size_t len;              ///< how many
} UdpSendArgs;

/** @brief RFC 768 "an indication of source port and source address": the sender a reply answers. */
typedef struct
{
    const struct protocore_udp_peer *peer; ///< the sender a reply answers
    char *ip_out;                          ///< where its address is formatted
    size_t ip_cap;                         ///< how much room that has
    uint16_t *port_out;                    ///< where its port is written
} UdpPeerArgs;

/**
 * @brief The receiving side of UDP.
 *
 * RFC 768 "User Interface": the creation of new receive ports, and receive operations on them that
 * return the data octets and an indication of source port and source address. A caller sets the
 * members a call takes, invokes it through ::UdpListener, and reads the outcome off the same handle.
 *
 * @var UdpListenerNs::port          the receive port a call acts on
 * @var UdpListenerNs::bind          what creating a receive port takes
 * @var UdpListenerNs::send_args     what sending a datagram takes
 * @var UdpListenerNs::peer_args     what a sender lookup reads and writes
 * @var UdpListenerNs::ok            a call's true/false outcome
 * @var UdpListenerNs::text          the group a lookup reports, or NULL
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
 * than ::PROTOCORE_UDP_RX_BUF_SIZE is refused.
 */
typedef struct
{
    uint16_t port; ///< the receive port every call names

    UdpBindArgs bind;      ///< what creating a receive port takes (RFC 768 User Interface)
    UdpSendArgs send_args; ///< what sending a datagram takes
    UdpPeerArgs peer_args; ///< what a sender lookup reads and writes

    proto_bool ok;
    const char *text;

    void (*const listen)(uint8_t *restrict work);
    void (*const listen_multicast)(uint8_t *restrict work);
    void (*const leave_multicast)(uint8_t *restrict work);
    void (*const poll)(uint8_t *restrict work);
    void (*const reply)(uint8_t *restrict work);
    void (*const peer_addr)(uint8_t *restrict work);
    void (*const sendto)(uint8_t *restrict work);
    void (*const close)(uint8_t *restrict work);
    void (*const joined_group)(uint8_t *restrict work);
} UdpListenerNs;

/** @brief The one symbol this module exports. */
extern UdpListenerNs UdpListener;

/**
 * @brief The PROTOCORE_UDP_LISTENER_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_udp_listener_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_UDP_SERVER_H
