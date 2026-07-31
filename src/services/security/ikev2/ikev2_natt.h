// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ikev2_natt.h
 * @brief IKEv2 NAT traversal - NAT detection (RFC 7296 §2.23) + UDP-encapsulation demux (RFC 3948).
 *
 * When a NAT sits between two IKE peers it rewrites the outer IP address / UDP port, which breaks ESP's
 * integrity check over the addresses and hides the peers from each other. IKEv2 detects this by each side
 * sending, in IKE_SA_INIT, two Notify payloads whose data is a hash binding the SA to the addresses each
 * side believes are in use:
 *
 *   NAT_DETECTION_SOURCE_IP      = SHA-1( SPIi | SPIr | source IP | source port )
 *   NAT_DETECTION_DESTINATION_IP = SHA-1( SPIi | SPIr | dest IP   | dest port   )
 *
 * The SPIs are taken in header order (initiator then responder). The receiver recomputes each hash over
 * the addresses it actually observes: a mismatch on the SOURCE hash means the peer's source was translated
 * (the peer is behind a NAT); a mismatch on the DESTINATION hash means our own address was translated (we
 * are behind a NAT). When either side is behind a NAT both move ESP into UDP on port 4500 (RFC 3948) and
 * the NATed side sends keepalives.
 *
 * This file is the host-testable NAT-T logic: the detection hash, the two Notify builders (over the tier-1
 * @ref pc_ike_notify_build), the mismatch tests, and the RFC 3948 marker / keepalive demux. The device
 * side only has to feed it the observed addresses off the socket.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IKEV2_NATT_H
#define PROTOCORE_IKEV2_NATT_H

#include "protocore_config.h"

#if PC_ENABLE_IKEV2

#include "services/security/ikev2/ikev2.h"
#include <stddef.h>
#include <stdint.h>

/** @brief NAT_DETECTION_SOURCE_IP notify message type (RFC 7296 §3.10.1). */
#define PC_IKE_N_NAT_DETECTION_SOURCE_IP 16388
/** @brief NAT_DETECTION_DESTINATION_IP notify message type (RFC 7296 §3.10.1). */
#define PC_IKE_N_NAT_DETECTION_DESTINATION_IP 16389
/** @brief NAT-detection hash length (SHA-1). */
#define PC_IKE_NATD_HASH_LEN 20
/** @brief The IKE / ESP UDP port used once NAT-T is active (RFC 3948). */
#define PC_NATT_PORT 4500
/** @brief The Non-ESP Marker length: IKE-in-UDP on port 4500 is prefixed with this many zero octets. */
#define PC_NATT_NON_ESP_MARKER_LEN 4
/** @brief A NAT-keepalive packet is this single byte (RFC 3948 §4). */
#define PC_NATT_KEEPALIVE_BYTE 0xFF

/**
 * @brief Compute a NAT-detection hash: SHA-1( @p init_spi | @p resp_spi | @p ip | @p port ) (RFC 7296 §2.23).
 * @param ip      the address octets (big-endian), @p ip_len long.
 * @param ip_len  4 (IPv4) or 16 (IPv6).
 * @param port    the UDP port (host order; encoded big-endian into the hash input).
 * @param out     receives @ref PC_IKE_NATD_HASH_LEN bytes.
 * @return @ref PC_IKE_NATD_HASH_LEN on success, or 0 on a null argument / bad @p ip_len.
 */
size_t pc_ike_natd_hash(const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                        const uint8_t *ip, size_t ip_len, uint16_t port, uint8_t out[PC_IKE_NATD_HASH_LEN]);

/**
 * @brief Build a NAT_DETECTION_SOURCE_IP Notify (the sender's own address) - RFC 7296 §2.23.
 * @return the payload length written, or 0 on overflow / bad argument.
 */
size_t pc_ike_natd_source_build(uint8_t *buf, size_t cap, IkePayloadType next_payload,
                                const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                                const uint8_t *src_ip, size_t ip_len, uint16_t src_port);

/**
 * @brief Build a NAT_DETECTION_DESTINATION_IP Notify (the address the sender is sending to) - RFC 7296 §2.23.
 * @return the payload length written, or 0 on overflow / bad argument.
 */
size_t pc_ike_natd_dest_build(uint8_t *buf, size_t cap, IkePayloadType next_payload,
                              const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                              const uint8_t *dst_ip, size_t ip_len, uint16_t dst_port);

/**
 * @brief True iff @p hash matches the NAT-detection hash recomputed over the given SPIs and address.
 *
 * A NAT-detection payload verifies (no translation on that axis) when this returns true; the two detection
 * helpers below express what a mismatch means.
 */
bool pc_ike_natd_match(const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                       const uint8_t *ip, size_t ip_len, uint16_t port, const uint8_t *hash);

/**
 * @brief The peer is behind a NAT: the received NAT_DETECTION_SOURCE_IP hash does not match the address the
 *        packet was actually observed to come from (its source was translated in transit).
 * @param observed_src_ip / @p observed_src_port  the packet's real source, as seen on our socket.
 * @param received_source_hash  the NAT_DETECTION_SOURCE_IP payload data from the peer.
 */
bool pc_ike_natd_peer_behind_nat(const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                                 const uint8_t *observed_src_ip, size_t ip_len, uint16_t observed_src_port,
                                 const uint8_t *received_source_hash);

/**
 * @brief We are behind a NAT: the received NAT_DETECTION_DESTINATION_IP hash does not match our own local
 *        address (the peer sent to a translated destination - our public address differs from our local one).
 * @param local_ip / @p local_port  our own address the packet arrived on.
 * @param received_dest_hash  the NAT_DETECTION_DESTINATION_IP payload data from the peer.
 */
bool pc_ike_natd_self_behind_nat(const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                                 const uint8_t *local_ip, size_t ip_len, uint16_t local_port,
                                 const uint8_t *received_dest_hash);

// ── RFC 3948 UDP-encapsulation demux (port 4500 carries IKE, ESP, and keepalives) ────────────────

/** @brief True iff @p p / @p len is a NAT-keepalive (a single 0xFF octet, RFC 3948 §4). */
bool pc_natt_is_keepalive(const uint8_t *p, size_t len);

/**
 * @brief True iff a UDP-4500 payload is an IKE message (it carries the 4-octet zero Non-ESP Marker).
 *
 * On port 4500 an ESP packet begins with its SPI (never zero), so a leading 32-bit zero distinguishes an
 * IKE message; the caller strips @ref PC_NATT_NON_ESP_MARKER_LEN octets before parsing it as IKE.
 */
bool pc_natt_is_ike(const uint8_t *p, size_t len);

#endif // PC_ENABLE_IKEV2
#endif // PROTOCORE_IKEV2_NATT_H
