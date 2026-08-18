// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file icmpv6.h
 * @brief Internet control messages for IPv6, RFC 4443.
 *
 * The same three leading fields as RFC 792, a renumbered type space, and a checksum that covers the
 * RFC 8200 sec 8.1 pseudo-header as well as the message. Read out of the caller's bytes; holds
 * nothing.
 */

#ifndef PROTOCORE_IDEMIP_ICMPV6_H
#define PROTOCORE_IDEMIP_ICMPV6_H

#include "idemIP/checksum.h"
#include "idemIP/ip/ipv6.h"

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// The common three fields, at the head of every message (RFC 4443 sec 2.1)
// ---------------------------------------------------------------------------

#define IDEMIP_ICMP6_OFF_TYPE 0u  ///< 8-bit Type
#define IDEMIP_ICMP6_OFF_CODE 1u  ///< 8-bit Code
#define IDEMIP_ICMP6_OFF_CKSUM 2u ///< 16-bit Checksum
#define IDEMIP_ICMP6_OFF_BODY 4u  ///< Message Body, its shape fixed by the type
#define IDEMIP_ICMP6_HDR_LEN 4u   ///< the part every type shares

/**
 * @brief The bit that sorts the two classes.
 *
 * RFC 4443 sec 2.1: "Error messages are identified as such by a zero in the high-order bit of their
 * message Type field values. Thus, error messages have message types from 0 to 127; informational
 * messages have message types from 128 to 255."
 */
#define IDEMIP_ICMP6_INFORMATIONAL 0x80u

/** @brief Message types RFC 4443 assigns. Numbered apart from RFC 792's. */
typedef enum PROTO_ENUM_PACKED
{
    IDEMIP_ICMP6_DEST_UNREACHABLE = 1,  ///< sec 3.1
    IDEMIP_ICMP6_PACKET_TOO_BIG = 2,    ///< sec 3.2
    IDEMIP_ICMP6_TIME_EXCEEDED = 3,     ///< sec 3.3
    IDEMIP_ICMP6_PARAMETER_PROBLEM = 4, ///< sec 3.4
    IDEMIP_ICMP6_ECHO_REQUEST = 128,    ///< sec 4.1
    IDEMIP_ICMP6_ECHO_REPLY = 129,      ///< sec 4.2
} IdemIpIcmp6Type;

// ---------------------------------------------------------------------------
// Codes (RFC 4443 sec 3)
// ---------------------------------------------------------------------------

/** @brief Destination Unreachable, sec 3.1. Codes 5 and 6 are "more informative subsets of code 1". */
#define IDEMIP_ICMP6_DU_NO_ROUTE 0u
#define IDEMIP_ICMP6_DU_PROHIBITED 1u
#define IDEMIP_ICMP6_DU_BEYOND_SCOPE 2u
#define IDEMIP_ICMP6_DU_ADDR_UNREACH 3u
#define IDEMIP_ICMP6_DU_PORT_UNREACH 4u
#define IDEMIP_ICMP6_DU_SRC_POLICY 5u
#define IDEMIP_ICMP6_DU_REJECT_ROUTE 6u

/** @brief Time Exceeded, sec 3.3. */
#define IDEMIP_ICMP6_TE_HOP_LIMIT 0u
#define IDEMIP_ICMP6_TE_REASSEMBLY 1u

/** @brief Parameter Problem, sec 3.4. Codes 1 and 2 are "more informative subsets of Code 0". */
#define IDEMIP_ICMP6_PP_ERRONEOUS_HDR 0u
#define IDEMIP_ICMP6_PP_UNREC_NEXT_HDR 1u
#define IDEMIP_ICMP6_PP_UNREC_OPTION 2u

/** @brief Echo Request and Echo Reply, sec 4.1 and sec 4.2: "Code 0". */
#define IDEMIP_ICMP6_CODE_ECHO 0u

/** @brief Packet Too Big, sec 3.2: "Set to 0 (zero) by the originator and ignored by the receiver." */
#define IDEMIP_ICMP6_CODE_PTB 0u

// ---------------------------------------------------------------------------
// Message bodies
// ---------------------------------------------------------------------------
// Every type here puts one 32-bit field at offset 4, then as much of the invoking packet as fits.
// Which field it is depends on the type: unused for Destination Unreachable and Time Exceeded, the
// next-hop MTU for Packet Too Big, and the octet offset of the fault for Parameter Problem.

#define IDEMIP_ICMP6_OFF_MTU 4u     ///< 32-bit MTU (sec 3.2)
#define IDEMIP_ICMP6_OFF_POINTER 4u ///< 32-bit Pointer (sec 3.4)
#define IDEMIP_ICMP6_OFF_UNUSED 4u  ///< 32-bit Unused, zero on transmission (sec 3.1, sec 3.3)
#define IDEMIP_ICMP6_ERR_HDR_LEN 8u ///< through that field; the invoking packet follows

#define IDEMIP_ICMP6_OFF_ID 4u       ///< 16-bit Identifier (sec 4.1)
#define IDEMIP_ICMP6_OFF_SEQ 6u      ///< 16-bit Sequence Number (sec 4.1)
#define IDEMIP_ICMP6_ECHO_HDR_LEN 8u ///< through the sequence number; data follows

/**
 * @brief How much of the invoking packet an error message may carry.
 *
 * RFC 4443 sec 3.1: "As much of invoking packet as possible without the ICMPv6 packet exceeding the
 * minimum IPv6 MTU", which leaves the IPv6 header and these eight octets out of the 1280.
 */
#define IDEMIP_ICMP6_ERR_QUOTE_MAX (IDEMIP_IPV6_MIN_MTU - IDEMIP_IPV6_HDR_LEN - IDEMIP_ICMP6_ERR_HDR_LEN)

/** @brief Type. */
PROTOCORE_INLINE uint8_t idemip_icmp6_type(const uint8_t *m)
{
    return m[IDEMIP_ICMP6_OFF_TYPE];
}

/** @brief Code; its meaning depends on the type. */
PROTOCORE_INLINE uint8_t idemip_icmp6_code(const uint8_t *m)
{
    return m[IDEMIP_ICMP6_OFF_CODE];
}

/** @brief Checksum as carried. */
PROTOCORE_INLINE uint16_t idemip_icmp6_cksum(const uint8_t *m)
{
    return idemip_rd16(m + IDEMIP_ICMP6_OFF_CKSUM);
}

/** @brief True for types 128 through 255, which are informational rather than errors. */
PROTOCORE_INLINE proto_bool idemip_icmp6_is_informational(const uint8_t *m)
{
    return (m[IDEMIP_ICMP6_OFF_TYPE] & IDEMIP_ICMP6_INFORMATIONAL) != 0u;
}

/** @brief Identifier (echo request and echo reply). */
PROTOCORE_INLINE uint16_t idemip_icmp6_id(const uint8_t *m)
{
    return idemip_rd16(m + IDEMIP_ICMP6_OFF_ID);
}

/** @brief Sequence Number (echo request and echo reply). */
PROTOCORE_INLINE uint16_t idemip_icmp6_seq(const uint8_t *m)
{
    return idemip_rd16(m + IDEMIP_ICMP6_OFF_SEQ);
}

/** @brief MTU of the next-hop link (Packet Too Big). */
PROTOCORE_INLINE uint32_t idemip_icmp6_mtu(const uint8_t *m)
{
    return idemip_rd32(m + IDEMIP_ICMP6_OFF_MTU);
}

/** @brief Octet offset within the invoking packet where the fault was found (Parameter Problem). */
PROTOCORE_INLINE uint32_t idemip_icmp6_pointer(const uint8_t *m)
{
    return idemip_rd32(m + IDEMIP_ICMP6_OFF_POINTER);
}

/**
 * @brief The checksum to write over @p len bytes of message at @p m, between @p src and @p dst.
 *
 * RFC 4443 sec 2.3: "the 16-bit one's complement of the one's complement sum of the entire ICMPv6
 * message, starting with the ICMPv6 message type field, and prepended with a 'pseudo-header' of
 * IPv6 header fields... The Next Header value used in the pseudo-header is 58."
 *
 * The caller zeroes the checksum field first. Unlike RFC 792, the addresses are covered, because
 * IPv6 carries no header checksum of its own.
 */
PROTOCORE_INLINE uint16_t idemip_icmp6_cksum_compute(const uint8_t *m, size_t len, const uint8_t *src,
                                                     const uint8_t *dst)
{
    uint32_t sum = idemip_ip6_pseudo_accum(0u, src, dst, (uint32_t)len, IDEMIP_IP6_NH_ICMPV6);
    return idemip_cksum_final(idemip_cksum_accum(sum, m, len));
}

static_assert(IDEMIP_ICMP6_OFF_CKSUM + 2u == IDEMIP_ICMP6_HDR_LEN,
              "the fields every RFC 4443 message shares must sum to the common header length");
static_assert(IDEMIP_ICMP6_OFF_SEQ + 2u == IDEMIP_ICMP6_ECHO_HDR_LEN,
              "the RFC 4443 sec 4.1 echo fields must sum to the echo header length");
static_assert(IDEMIP_ICMP6_ECHO_REQUEST >= IDEMIP_ICMP6_INFORMATIONAL,
              "RFC 4443 sec 2.1 puts the informational types at 128 and above");
static_assert(IDEMIP_ICMP6_PARAMETER_PROBLEM < IDEMIP_ICMP6_INFORMATIONAL,
              "RFC 4443 sec 2.1 puts the error types below 128");

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_ICMPV6_H
