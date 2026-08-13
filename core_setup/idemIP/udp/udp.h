// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp.h
 * @brief The user datagram header, RFC 768.
 *
 * Field offsets and the pseudo-header the checksum covers, read out of the caller's bytes.
 */

#ifndef PROTOCORE_IDEMIP_UDP_H
#define PROTOCORE_IDEMIP_UDP_H

#include "idemIP/checksum.h"
#include "idemIP/ip/ipv4.h"

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Field offsets (RFC 768, "Format")
// ---------------------------------------------------------------------------

#define IDEMIP_UDP_OFF_SRC_PORT 0u ///< 16-bit Source Port
#define IDEMIP_UDP_OFF_DST_PORT 2u ///< 16-bit Destination Port
#define IDEMIP_UDP_OFF_LEN 4u      ///< 16-bit Length
#define IDEMIP_UDP_OFF_CKSUM 6u    ///< 16-bit Checksum
#define IDEMIP_UDP_HDR_LEN 8u      ///< the header, data follows

/**
 * @brief RFC 768: Length "is the length in octets of this user datagram including this header and
 * the data. (This means the minimum value of the length is eight.)"
 */
#define IDEMIP_UDP_LEN_MIN IDEMIP_UDP_HDR_LEN

/**
 * @brief RFC 768: "An all zero transmitted checksum value means that the transmitter generated no
 * checksum (for debugging or for higher level protocols that don't care)."
 */
#define IDEMIP_UDP_CKSUM_NONE 0x0000u

/**
 * @brief RFC 768: "If the computed checksum is zero, it is transmitted as all ones (the equivalent
 * in one's complement arithmetic)."
 *
 * Zero already means "no checksum", so a real result of zero is sent as its other representation
 * rather than being mistaken for an absent one.
 */
#define IDEMIP_UDP_CKSUM_ZERO_AS 0xFFFFu

/** @brief Source Port. RFC 768: optional; "If not used, a value of zero is inserted." */
PROTOCORE_INLINE uint16_t idemip_udp_src_port(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_UDP_OFF_SRC_PORT);
}

/** @brief Destination Port. */
PROTOCORE_INLINE uint16_t idemip_udp_dst_port(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_UDP_OFF_DST_PORT);
}

/** @brief Length: this header plus the data. */
PROTOCORE_INLINE uint16_t idemip_udp_len(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_UDP_OFF_LEN);
}

/** @brief Checksum as carried; IDEMIP_UDP_CKSUM_NONE means the sender computed none. */
PROTOCORE_INLINE uint16_t idemip_udp_cksum(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_UDP_OFF_CKSUM);
}

/**
 * @brief Accumulate the pseudo-header RFC 768 prefixes to the datagram for the checksum.
 *
 * "The pseudo header conceptually prefixed to the UDP header contains the source address, the
 * destination address, the protocol, and the UDP length. This information gives protection against
 * misrouted datagrams." It is a zero octet, the protocol, and the length, after the two addresses.
 *
 * Not part of the datagram: it is summed, never sent, so this accumulates into a running sum the
 * caller then carries over the header and data.
 */
PROTOCORE_INLINE uint32_t idemip_udp_pseudo_accum(uint32_t sum, uint32_t src, uint32_t dst, uint16_t udp_len)
{
    sum += (src >> 16) & 0xFFFFu;
    sum += src & 0xFFFFu;
    sum += (dst >> 16) & 0xFFFFu;
    sum += dst & 0xFFFFu;
    sum += (uint32_t)IDEMIP_IP4_PROTO_UDP; // the zero octet leaves the protocol as the low half
    sum += (uint32_t)udp_len;
    return sum;
}

/**
 * @brief The checksum to write, over the pseudo-header and @p len bytes of datagram at @p h.
 *
 * The caller zeroes the checksum field first; RFC 1071's sum is over the datagram as it will be
 * sent, and the field is part of it.
 */
PROTOCORE_INLINE uint16_t idemip_udp_cksum_compute(const uint8_t *h, size_t len, uint32_t src, uint32_t dst)
{
    uint32_t sum = idemip_udp_pseudo_accum(0u, src, dst, (uint16_t)len);
    uint16_t c = idemip_cksum_final(idemip_cksum_accum(sum, h, len));
    return (c == 0u) ? (uint16_t)IDEMIP_UDP_CKSUM_ZERO_AS : c;
}

static_assert(IDEMIP_UDP_OFF_CKSUM + 2u == IDEMIP_UDP_HDR_LEN,
              "the RFC 768 field offsets must sum to the header length");

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_UDP_H
