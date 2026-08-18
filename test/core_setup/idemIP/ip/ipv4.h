// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ipv4.h
 * @brief The internet header, RFC 791 sec 3.1.
 *
 * Field offsets and the masks that split the packed ones, read out of the caller's bytes. Nothing
 * here stores or moves anything.
 */

#ifndef PROTOCORE_IDEMIP_IPV4_H
#define PROTOCORE_IDEMIP_IPV4_H

#include "idemIP/common.h"

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Field offsets (RFC 791 sec 3.1, Figure 4)
// ---------------------------------------------------------------------------

#define IDEMIP_IP4_OFF_VER_IHL 0u    ///< 4-bit Version, 4-bit IHL
#define IDEMIP_IP4_OFF_TOS 1u        ///< 8-bit Type of Service
#define IDEMIP_IP4_OFF_TOTAL_LEN 2u  ///< 16-bit Total Length
#define IDEMIP_IP4_OFF_ID 4u         ///< 16-bit Identification
#define IDEMIP_IP4_OFF_FLAGS_FRAG 6u ///< 3-bit Flags, 13-bit Fragment Offset
#define IDEMIP_IP4_OFF_TTL 8u        ///< 8-bit Time to Live
#define IDEMIP_IP4_OFF_PROTO 9u      ///< 8-bit Protocol
#define IDEMIP_IP4_OFF_CKSUM 10u     ///< 16-bit Header Checksum
#define IDEMIP_IP4_OFF_SRC 12u       ///< 32-bit Source Address
#define IDEMIP_IP4_OFF_DST 16u       ///< 32-bit Destination Address
#define IDEMIP_IP4_OFF_OPTIONS 20u   ///< Options, then padding to a 32-bit boundary

// ---------------------------------------------------------------------------
// Version and IHL, the first octet
// ---------------------------------------------------------------------------

/** @brief RFC 791 sec 3.1: "This document describes version 4." */
#define IDEMIP_IP4_VERSION 4u

#define IDEMIP_IP4_VER_SHIFT 4u
#define IDEMIP_IP4_VER_MASK 0x0Fu
#define IDEMIP_IP4_IHL_MASK 0x0Fu

/**
 * @brief RFC 791 sec 3.1: "Internet Header Length is the length of the internet header in 32 bit
 * words... the minimum value for a correct header is 5."
 */
#define IDEMIP_IP4_IHL_MIN 5u

/** @brief IHL is 4 bits, so the header cannot exceed 15 words. */
#define IDEMIP_IP4_IHL_MAX 15u

/** @brief Header bytes for an IHL of @p ihl words. */
#define IDEMIP_IP4_HDR_BYTES(ihl) ((size_t)(ihl) * 4u)

// ---------------------------------------------------------------------------
// Flags and Fragment Offset, the seventh and eighth octets
// ---------------------------------------------------------------------------
// RFC 791 sec 3.1: "Bit 0: reserved, must be zero. Bit 1: (DF) 0 = May Fragment, 1 = Don't
// Fragment. Bit 2: (MF) 0 = Last Fragment, 1 = More Fragments." The three sit above a 13-bit
// fragment offset in one 16-bit field, so both come out of a single read.

#define IDEMIP_IP4_FLAG_RESERVED (1u << 15) ///< must be zero
#define IDEMIP_IP4_FLAG_DF (1u << 14)       ///< don't fragment
#define IDEMIP_IP4_FLAG_MF (1u << 13)       ///< more fragments
#define IDEMIP_IP4_FRAG_OFF_MASK 0x1FFFu    ///< the 13-bit fragment offset

/**
 * @brief RFC 791 sec 3.1: the fragment offset "is measured in units of 8 octets (64 bits)", so the
 * field counts eight-byte units and the byte position is the field shifted up by three.
 */
#define IDEMIP_IP4_FRAG_UNIT 8u

// ---------------------------------------------------------------------------
// Protocol numbers this tree carries (RFC 791 sec 3.1, assigned by IANA)
// ---------------------------------------------------------------------------

#define IDEMIP_IP4_PROTO_ICMP 1u
#define IDEMIP_IP4_PROTO_TCP 6u
#define IDEMIP_IP4_PROTO_UDP 17u

// ---------------------------------------------------------------------------
// Accessors: every multi-byte field is assembled from its bytes
// ---------------------------------------------------------------------------
// The source and destination addresses sit at 12 and 16 of a header that itself starts 14 bytes
// into an Ethernet frame, so neither is aligned for a 32-bit read in the frame it arrived in.

/** @brief Version, the high nibble of the first octet. */
PROTOCORE_INLINE uint8_t idemip_ip4_version(const uint8_t *h)
{
    return (uint8_t)((h[IDEMIP_IP4_OFF_VER_IHL] >> IDEMIP_IP4_VER_SHIFT) & IDEMIP_IP4_VER_MASK);
}

/** @brief IHL in 32-bit words, the low nibble of the first octet. */
PROTOCORE_INLINE uint8_t idemip_ip4_ihl(const uint8_t *h)
{
    return (uint8_t)(h[IDEMIP_IP4_OFF_VER_IHL] & IDEMIP_IP4_IHL_MASK);
}

/** @brief Total Length: the whole datagram, header included. */
PROTOCORE_INLINE uint16_t idemip_ip4_total_len(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_IP4_OFF_TOTAL_LEN);
}

/** @brief The three flags and the fragment offset, as one field. */
PROTOCORE_INLINE uint16_t idemip_ip4_flags_frag(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_IP4_OFF_FLAGS_FRAG);
}

/** @brief Fragment offset in bytes: the field counts units of eight octets. */
PROTOCORE_INLINE uint32_t idemip_ip4_frag_offset_bytes(const uint8_t *h)
{
    return (uint32_t)(idemip_ip4_flags_frag(h) & IDEMIP_IP4_FRAG_OFF_MASK) * IDEMIP_IP4_FRAG_UNIT;
}

/** @brief Protocol carried in the data field. */
PROTOCORE_INLINE uint8_t idemip_ip4_proto(const uint8_t *h)
{
    return h[IDEMIP_IP4_OFF_PROTO];
}

/** @brief Source address. */
PROTOCORE_INLINE uint32_t idemip_ip4_src(const uint8_t *h)
{
    return idemip_rd32(h + IDEMIP_IP4_OFF_SRC);
}

/** @brief Destination address. */
PROTOCORE_INLINE uint32_t idemip_ip4_dst(const uint8_t *h)
{
    return idemip_rd32(h + IDEMIP_IP4_OFF_DST);
}

static_assert(IDEMIP_IP4_OFF_OPTIONS == IDEMIP_IPV4_HDR_LEN,
              "the RFC 791 field offsets must sum to the option-free header length");
static_assert(IDEMIP_IP4_HDR_BYTES(IDEMIP_IP4_IHL_MIN) == IDEMIP_IPV4_HDR_LEN,
              "the minimum IHL of 5 words is the option-free header (RFC 791 sec 3.1)");

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_IPV4_H
