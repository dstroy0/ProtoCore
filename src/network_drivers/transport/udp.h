// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp.h
 * @brief Layer 4 (Transport) - connectionless UDP datagram service.
 *
 * The transport layer's UDP counterpart to the TCP connection pool (see
 * tcp.h / listener.h). It owns *all* lwIP UDP plumbing (`udp_pcb`,
 * `pbuf`); higher layers (application services such as SNMP and the captive
 * portal's DNS responder) bind a port and exchange datagrams through this API
 * without ever touching lwIP. This keeps the OSI layering honest: transport
 * concerns stay in the transport layer.
 *
 * Datagram delivery is callback-driven from the lwIP thread (no per-loop
 * polling). The handler receives a contiguous payload and an opaque peer token
 * valid only for the duration of the call; reply synchronously with
 * pc_udp_send(). On non-Arduino (host) builds the functions are no-op stubs so
 * the services that use them stay host-compilable.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_H
#define PROTOCORE_UDP_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Opaque sender of a received datagram.
 *
 * Wraps the lwIP source address / port / PCB. Valid only inside the
 * pc_udp_handler call; pass it to pc_udp_send() to reply. The concrete layout
 * lives in udp.cpp so no lwIP type escapes the transport layer.
 */
struct pc_udp_peer;

/**
 * @brief Datagram handler invoked for each received UDP packet.
 *
 * @param data  contiguous datagram payload (transport-owned scratch).
 * @param len   payload length in bytes.
 * @param peer  reply token (valid only during this call).
 * @param ctx   the opaque context passed to pc_udp_listen().
 */
typedef void (*pc_udp_handler)(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx);

/**
 * @brief Bind a UDP port and route incoming datagrams to @p handler.
 *
 * @param port     UDP port to bind (e.g. 161 for SNMP, 53 for captive DNS).
 * @param handler  callback for each datagram.
 * @param ctx      opaque pointer forwarded to @p handler.
 * @return true on success; false if no listener slot is free, the bind fails,
 *         or UDP is unavailable (host builds). ESP32 only; a host build returns false.
 */
bool pc_udp_listen(uint16_t port, pc_udp_handler handler, void *ctx);

/**
 * @brief Bind a UDP port, join an IPv4 multicast group, and route group datagrams to @p handler.
 *
 * The multicast counterpart to pc_udp_listen(): as well as binding the port it joins @p group_ip
 * via IGMP on every interface, so the stack accepts datagrams addressed to the group rather than to
 * this host. That is what lets a service *observe* a shared protocol - counting mDNS announcements
 * on 224.0.0.251:5353, watching SSDP on 239.255.255.250:1900 - instead of only its own traffic.
 *
 * The socket is bound with SO_REUSEADDR, because a well-known multicast port is normally already
 * bound by whoever implements that protocol (the ESP-IDF `mdns` component holds 5353). Without it
 * the second bind fails and the observer never sees a packet.
 *
 * Observation only: joining a group does not make this device a responder for it. Reply with
 * pc_udp_send() only if the protocol expects it.
 *
 * @param group_ip IPv4 multicast group, dotted-quad (224.0.0.0/4).
 * @param port     UDP port to bind (e.g. 5353 for mDNS).
 * @param handler  callback for each datagram received on the group.
 * @param ctx      opaque pointer forwarded to @p handler.
 * @return true if the port bound and the group was joined; false if the pool is full, @p group_ip
 *         is not a multicast address, IGMP is unavailable, or on a host build.
 */
bool pc_udp_listen_multicast(const char *group_ip, uint16_t port, pc_udp_handler handler, void *ctx);

/**
 * @brief Leave the multicast group joined on @p port and release its listener slot.
 * @return true if a matching multicast listener was found and torn down.
 */
bool pc_udp_leave_multicast(uint16_t port);

/**
 * @brief Send a datagram back to the peer captured during the handler call.
 *
 * @param peer  the token handed to the pc_udp_handler.
 * @param data  payload bytes.
 * @param len   payload length.
 * @return true if the datagram was queued for transmission.
 */
bool pc_udp_send(const struct pc_udp_peer *peer, const uint8_t *data, size_t len);

/**
 * @brief Send a UDP datagram to an arbitrary destination (outbound client).
 *
 * Unlike pc_udp_send() - which replies to the peer of a received datagram -
 * this sends to a host given as a dotted-quad IPv4 string and port, using a
 * single shared outbound PCB. Fire-and-forget; for clients such as the syslog
 * sender. ESP32 only; a host build returns false.
 *
 * @param dst_ip   destination IPv4 address (e.g. "192.168.1.10").
 * @param dst_port destination UDP port.
 * @param data     payload bytes.
 * @param len      payload length.
 * @return true if the datagram was queued; false on a bad address, allocation
 *         failure, or a host build.
 */
bool pc_udp_sendto(const char *dst_ip, uint16_t dst_port, const uint8_t *data, size_t len);

/**
 * @brief Copy a received peer's source IPv4 address (dotted-quad) and port out.
 *
 * The pc_udp_peer token is valid only inside the handler; a service that wants to
 * message the peer later (e.g. CoAP Observe notifications) captures its address
 * here and sends with pc_udp_listener_sendto(). @return false on a host build or
 * a too-small buffer.
 */
bool pc_udp_peer_addr(const struct pc_udp_peer *peer, char *ip_out, size_t ip_cap, uint16_t *port_out);

/**
 * @brief Send a datagram from the listener bound on @p listen_port to an address.
 *
 * Unlike pc_udp_sendto() (a shared ephemeral source port), this uses the bound
 * listener's PCB, so the datagram's source is @p listen_port - required when a
 * peer matches replies by the server endpoint (CoAP Observe notifications come
 * from :5683). @return false if no listener is bound on @p listen_port.
 */
bool pc_udp_listener_sendto(uint16_t listen_port, const char *dst_ip, uint16_t dst_port, const uint8_t *data,
                            size_t len);

#if !defined(ARDUINO)
// Host-only test hooks: capture the last datagram passed to pc_udp_sendto() so a
// unit test can inspect what an outbound UDP service (SNMP trap/inform, syslog,
// telemetry) actually built. Off by default; enable per test. Mirrors the
// tcp_capture() seam in the lwIP mock.
void pc_udp_capture_enable();
void pc_udp_capture_reset();
const uint8_t *pc_udp_captured(); ///< bytes of the last captured datagram (nullptr if none)
size_t pc_udp_captured_len();     ///< length of the last captured datagram

// Deliver a datagram from (src_ip, src_port) to the handler bound to listen_port, driving the
// receive path of a UDP service (CoAP Observe, DNS, SNMP) as if a peer had sent it.
void pc_udp_inject(uint16_t listen_port, const char *src_ip, uint16_t src_port, const uint8_t *data, size_t len);
void pc_udp_set_listener_sendto_result(bool ok); ///< force pc_udp_listener_sendto() to fail (unreachable peer)
void pc_udp_reset_listeners();                   ///< clear all bound listeners (per-test isolation)

/** @brief The multicast group joined on @p port, or nullptr if that port has no multicast listener. */
const char *pc_udp_joined_group(uint16_t port);
#endif

#endif // PROTOCORE_UDP_H
