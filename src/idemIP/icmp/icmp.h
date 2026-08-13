// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file icmp.h
 * @brief Internet control messages, RFC 792.
 *
 * Every message begins Type, Code, Checksum; what follows depends on the type. Field offsets and
 * accessors over the caller's bytes.
 */

#ifndef PROTOCORE_IDEMIP_ICMP_H
#define PROTOCORE_IDEMIP_ICMP_H

#include "idemIP/checksum.h"
#include "idemIP/ip/ipv4.h"

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// The common three fields, at the head of every message (RFC 792)
// ---------------------------------------------------------------------------

#define IDEMIP_ICMP_OFF_TYPE 0u  ///< 8-bit Type
#define IDEMIP_ICMP_OFF_CODE 1u  ///< 8-bit Code
#define IDEMIP_ICMP_OFF_CKSUM 2u ///< 16-bit Checksum
#define IDEMIP_ICMP_HDR_LEN 4u   ///< the part every type shares

/** @brief Message types RFC 792 assigns. */
typedef enum PROTO_ENUM_PACKED
{
    IDEMIP_ICMP_ECHO_REPLY = 0, ///< "0 for echo reply message"
    IDEMIP_ICMP_DEST_UNREACHABLE = 3,
    IDEMIP_ICMP_SOURCE_QUENCH = 4,
    IDEMIP_ICMP_REDIRECT = 5,
    IDEMIP_ICMP_ECHO = 8, ///< "8 for echo message"
    IDEMIP_ICMP_TIME_EXCEEDED = 11,
    IDEMIP_ICMP_PARAMETER_PROBLEM = 12,
    IDEMIP_ICMP_TIMESTAMP = 13,
    IDEMIP_ICMP_TIMESTAMP_REPLY = 14,
    IDEMIP_ICMP_INFO_REQUEST = 15,
    IDEMIP_ICMP_INFO_REPLY = 16,
} IdemIpIcmpType;

// ---------------------------------------------------------------------------
// Echo or Echo Reply (RFC 792), the type this end answers
// ---------------------------------------------------------------------------
// Type, Code, Checksum, then Identifier and Sequence Number, then data.

#define IDEMIP_ICMP_OFF_ID 4u       ///< 16-bit Identifier
#define IDEMIP_ICMP_OFF_SEQ 6u      ///< 16-bit Sequence Number
#define IDEMIP_ICMP_ECHO_HDR_LEN 8u ///< through the sequence number; data follows

/** @brief RFC 792, Echo or Echo Reply: "Code 0". */
#define IDEMIP_ICMP_CODE_ECHO 0u

/** @brief Type. */
PROTOCORE_INLINE uint8_t idemip_icmp_type(const uint8_t *m)
{
    return m[IDEMIP_ICMP_OFF_TYPE];
}

/** @brief Code; its meaning depends on the type. */
PROTOCORE_INLINE uint8_t idemip_icmp_code(const uint8_t *m)
{
    return m[IDEMIP_ICMP_OFF_CODE];
}

/** @brief Checksum as carried. */
PROTOCORE_INLINE uint16_t idemip_icmp_cksum(const uint8_t *m)
{
    return idemip_rd16(m + IDEMIP_ICMP_OFF_CKSUM);
}

/** @brief Identifier (echo and echo reply). */
PROTOCORE_INLINE uint16_t idemip_icmp_id(const uint8_t *m)
{
    return idemip_rd16(m + IDEMIP_ICMP_OFF_ID);
}

/** @brief Sequence Number (echo and echo reply). */
PROTOCORE_INLINE uint16_t idemip_icmp_seq(const uint8_t *m)
{
    return idemip_rd16(m + IDEMIP_ICMP_OFF_SEQ);
}

/**
 * @brief The checksum to write over @p len bytes of message at @p m.
 *
 * RFC 792: "The checksum is the 16-bit ones's complement of the one's complement sum of the ICMP
 * message starting with the ICMP Type. For computing the checksum, the checksum field should be
 * zero. If the total length is odd, the received data is padded with one octet of zeros."
 *
 * No pseudo-header: unlike UDP and TCP, this covers the message alone.
 */
PROTOCORE_INLINE uint16_t idemip_icmp_cksum_compute(const uint8_t *m, size_t len)
{
    return idemip_cksum(m, len);
}

static_assert(IDEMIP_ICMP_OFF_CKSUM + 2u == IDEMIP_ICMP_HDR_LEN,
              "the fields every RFC 792 message shares must sum to the common header length");
static_assert(IDEMIP_ICMP_OFF_SEQ + 2u == IDEMIP_ICMP_ECHO_HDR_LEN,
              "the RFC 792 echo fields must sum to the echo header length");

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_ICMP_H
