// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ikev2_natt.h
 * @brief IKEv2 NAT traversal: NAT detection (RFC 7296 sec 2.23) and the UDP encapsulation demux
 *        (RFC 3948 sec 2).
 *
 * RFC 7296 sec 2.23: both peers put NAT_DETECTION_SOURCE_IP and NAT_DETECTION_DESTINATION_IP Notify
 * payloads in their IKE_SA_INIT messages, just after Ni and Nr. The data of the first is a SHA-1
 * digest of the SPIs in the order they appear in the header, the IP address, and the port the packet
 * was sent from; the data of the second is the same digest over the address and port it was sent to.
 * The Notify Message Types are 16388 and 16389 (sec 3.10.1).
 *
 * A recipient recomputes each digest over the addresses it actually observes. No match on any
 * received NAT_DETECTION_SOURCE_IP means the peer's source was translated, so the peer is behind a
 * NAT. A mismatching NAT_DETECTION_DESTINATION_IP means this system is behind a NAT and should send
 * the keepalives of RFC 3948. Once a NAT is detected both peers move to port 4500 and encapsulate
 * ESP in UDP.
 *
 * RFC 3948 sec 2.2: an IKE message on port 4500 is prefixed with the Non-ESP Marker, four zero
 * octets aligned with the SPI field of an ESP packet, and sec 2.1 requires that SPI to be non-zero,
 * so the marker separates IKE from ESP. RFC 3948 sec 2.3: a NAT-keepalive is a one octet payload
 * with the value 0xFF.
 *
 * The module exports one symbol, @ref IkeNatt. Everything in ikev2_natt.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IKEV2_NATT_H
#define PROTOCORE_IKEV2_NATT_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_IKEV2

PROTOCORE_BEGIN_DECLS

#include "services/security/ikev2/ikev2.h" // IkePayloadType: the Next Payload a detection Notify carries
// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

/** @brief NAT_DETECTION_SOURCE_IP Notify Message Type (RFC 7296 sec 3.10.1). */
#define PROTOCORE_IKE_N_NAT_DETECTION_SOURCE_IP 16388
/** @brief NAT_DETECTION_DESTINATION_IP Notify Message Type (RFC 7296 sec 3.10.1). */
#define PROTOCORE_IKE_N_NAT_DETECTION_DESTINATION_IP 16389
/** @brief Length of the SHA-1 digest a detection payload carries (RFC 7296 sec 2.23). */
#define PROTOCORE_IKE_NATD_HASH_LEN 20
/** @brief The UDP port reserved for UDP-encapsulated ESP and IKE (RFC 3948 sec 2.1, sec 2.2). */
#define PROTOCORE_NATT_PORT 4500
/** @brief Non-ESP Marker length: four zero octets before an IKE message on port 4500 (RFC 3948 sec 2.2). */
#define PROTOCORE_NATT_NON_ESP_MARKER_LEN 4
/** @brief The single octet a NAT-keepalive carries (RFC 3948 sec 2.3). */
#define PROTOCORE_NATT_KEEPALIVE_BYTE 0xFF

// ---------------------------------------------------------------------------
// Typedefs
// ---------------------------------------------------------------------------

/** @brief The SPIs a digest covers, in the order they appear in the header (RFC 7296 sec 2.23). */
typedef struct
{
    const uint8_t *init_spi; ///< IKE SA Initiator's SPI
    const uint8_t *resp_spi; ///< IKE SA Responder's SPI
} IkeNattSpiArgs;

/** @brief The address and port a digest covers (RFC 7296 sec 2.23). */
typedef struct
{
    const uint8_t *ip; ///< the address octets, big endian
    size_t ip_len;     ///< 4 for IPv4 or 16 for IPv6
    uint16_t port;     ///< the UDP port, host order, encoded big endian into the digest
} IkeNattAddrArgs;

/** @brief Where a digest lands, and the one a compare judges (RFC 7296 sec 2.23). */
typedef struct
{
    uint8_t *out;            ///< receives PROTOCORE_IKE_NATD_HASH_LEN octets
    const uint8_t *received; ///< the Notification Data from the peer
} IkeNattDigestArgs;

/** @brief Where a Notify payload is written (RFC 7296 sec 3.10). */
typedef struct
{
    uint8_t *buf;                ///< where the payload is written
    size_t cap;                  ///< room there
    IkePayloadType next_payload; ///< Next Payload: the type of the payload that follows this one
} IkeNattOutArgs;

/** @brief The UDP payload the port 4500 demux judges (RFC 3948 sec 2.1, 2.2, 2.3). */
typedef struct
{
    const uint8_t *p; ///< the datagram payload
    size_t len;       ///< its length
} IkeNattPktArgs;

/** @brief The NAT traversal calls, described only in ikev2_natt.c. */
struct IkeNattInternal;

/**
 * @brief The IKEv2 NAT traversal handle (RFC 7296 sec 2.23, RFC 3948).
 *
 * A caller sets the members a call takes, invokes it through ::IkeNatt, and reads the outcome off
 * the same handle.
 *
 * No storage member: the addresses come off the socket and the payload buffers are the caller's, so
 * nothing survives a call.
 *
 * @var IkeNattNs::spi     the SPIs a digest covers, in header order
 * @var IkeNattNs::addr    the address and port a digest covers
 * @var IkeNattNs::digest  where a digest lands, and the one a compare judges
 * @var IkeNattNs::out     where a Notify payload is written
 * @var IkeNattNs::pkt     the UDP payload the port 4500 demux judges
 * @var IkeNattNs::ok      a call's true/false outcome
 * @var IkeNattNs::n       octets written, zero on failure
 * @var IkeNattNs::hash    SHA-1(SPIi | SPIr | IP | Port) into @c digest.out (sec 2.23)
 * @var IkeNattNs::source_build  write a NAT_DETECTION_SOURCE_IP Notify over the sender's own address
 * @var IkeNattNs::dest_build    write a NAT_DETECTION_DESTINATION_IP Notify over the address sent to
 * @var IkeNattNs::match         @c digest.received equals the digest over @c spi and @c addr
 * @var IkeNattNs::peer_behind_nat  the received source digest does not match the observed source
 * @var IkeNattNs::self_behind_nat  the received destination digest does not match our own address
 * @var IkeNattNs::is_keepalive  the payload is the one octet 0xFF (RFC 3948 sec 2.3)
 * @var IkeNattNs::is_ike        the payload carries the Non-ESP Marker (RFC 3948 sec 2.2)
 * @var IkeNattNs::internal  the calls that read this handle
 */
typedef struct
{
    IkeNattSpiArgs spi;       ///< the SPIs a digest covers (sec 2.23)
    IkeNattAddrArgs addr;     ///< the address and port a digest covers (sec 2.23)
    IkeNattDigestArgs digest; ///< where a digest lands and the one a compare judges
    IkeNattOutArgs out;       ///< where a Notify payload is written (sec 3.10)
    IkeNattPktArgs pkt;       ///< the UDP payload the demux judges (RFC 3948 sec 2)

    proto_bool ok;
    size_t n;

    void (*hash)(struct IkeNattInternal *ctx);
    void (*source_build)(struct IkeNattInternal *ctx);
    void (*dest_build)(struct IkeNattInternal *ctx);
    void (*match)(struct IkeNattInternal *ctx);
    void (*peer_behind_nat)(struct IkeNattInternal *ctx);
    void (*self_behind_nat)(struct IkeNattInternal *ctx);
    void (*is_keepalive)(struct IkeNattInternal *ctx);
    void (*is_ike)(struct IkeNattInternal *ctx);

    struct IkeNattInternal *internal;
} IkeNattNs;

/** @brief The one symbol this module exports. */
extern IkeNattNs IkeNatt;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IKEV2

#endif // PROTOCORE_IKEV2_NATT_H
