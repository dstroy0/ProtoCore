// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tcp.h
 * @brief The TCP header, RFC 9293 sec 3.1.
 *
 * Field offsets, the control bits, the options RFC 9293 sec 3.2 requires, and the pseudo-header the
 * checksum covers. Read out of the caller's bytes; holds nothing.
 */

#ifndef PROTOCORE_IDEMIP_TCP_H
#define PROTOCORE_IDEMIP_TCP_H

#include "idemIP/checksum.h"
#include "idemIP/ip/ipv4.h"

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Field offsets (RFC 9293 sec 3.1, Figure 1)
// ---------------------------------------------------------------------------

#define IDEMIP_TCP_OFF_SRC_PORT 0u    ///< 16-bit Source Port
#define IDEMIP_TCP_OFF_DST_PORT 2u    ///< 16-bit Destination Port
#define IDEMIP_TCP_OFF_SEQ 4u         ///< 32-bit Sequence Number
#define IDEMIP_TCP_OFF_ACK 8u         ///< 32-bit Acknowledgment Number
#define IDEMIP_TCP_OFF_OFFS_FLAGS 12u ///< 4-bit Data Offset, 4-bit Reserved, 8 control bits
#define IDEMIP_TCP_OFF_WINDOW 14u     ///< 16-bit Window
#define IDEMIP_TCP_OFF_CKSUM 16u      ///< 16-bit Checksum
#define IDEMIP_TCP_OFF_URGENT 18u     ///< 16-bit Urgent Pointer
#define IDEMIP_TCP_OFF_OPTIONS 20u    ///< Options, then data

// ---------------------------------------------------------------------------
// Data Offset (RFC 9293 sec 3.1)
// ---------------------------------------------------------------------------
// "The number of 32-bit words in the TCP header. This indicates where the data begins. The TCP
// header (even one including options) is an integer multiple of 32 bits long."

#define IDEMIP_TCP_DOFF_SHIFT 12u
#define IDEMIP_TCP_DOFF_MASK 0x0Fu

/** @brief An option-free header is five 32-bit words. */
#define IDEMIP_TCP_DOFF_MIN 5u

/** @brief Data Offset is 4 bits, so the header cannot exceed fifteen words. */
#define IDEMIP_TCP_DOFF_MAX 15u

/** @brief Header bytes for a Data Offset of @p doff words. */
#define IDEMIP_TCP_HDR_BYTES(doff) ((size_t)(doff) * 4u)

/**
 * @brief RFC 9293 sec 3.1: Reserved "Must be zero in generated segments and must be ignored in
 * received segments if the corresponding future features are not implemented".
 */
#define IDEMIP_TCP_RSRVD_MASK 0x0F00u

// ---------------------------------------------------------------------------
// Control bits (RFC 9293 sec 3.1)
// ---------------------------------------------------------------------------
// Assignment is IANA's; these are the eight currently assigned, in the order the figure lays them
// out above the window.

#define IDEMIP_TCP_CWR (1u << 7) ///< Congestion Window Reduced
#define IDEMIP_TCP_ECE (1u << 6) ///< ECN-Echo
#define IDEMIP_TCP_URG (1u << 5) ///< Urgent pointer field is significant
#define IDEMIP_TCP_ACK (1u << 4) ///< Acknowledgment field is significant
#define IDEMIP_TCP_PSH (1u << 3) ///< Push function
#define IDEMIP_TCP_RST (1u << 2) ///< Reset the connection
#define IDEMIP_TCP_SYN (1u << 1) ///< Synchronize sequence numbers
#define IDEMIP_TCP_FIN (1u << 0) ///< No more data from sender

// ---------------------------------------------------------------------------
// Options (RFC 9293 sec 3.2)
// ---------------------------------------------------------------------------
// Kind 0 and 1 are single octets; every other kind carries its own length octet, which is what
// lets a parser step past an option it does not implement.

#define IDEMIP_TCP_OPT_END 0u     ///< End of Option List
#define IDEMIP_TCP_OPT_NOP 1u     ///< No-Operation
#define IDEMIP_TCP_OPT_MSS 2u     ///< Maximum Segment Size
#define IDEMIP_TCP_OPT_MSS_LEN 4u ///< kind, length, and a 16-bit value

/**
 * @brief RFC 9293 sec 3.7.1 MUST-14: "TCP endpoints MUST implement both sending and receiving the
 * MSS Option."
 */
#define IDEMIP_TCP_MSS_KIND IDEMIP_TCP_OPT_MSS

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
// Sequence and acknowledgment sit at 4 and 8 of a header that starts 20 bytes into an IPv4
// datagram that itself starts 14 into a frame - so 32-bit fields land on a two-byte boundary.

/** @brief Source Port. */
PROTOCORE_INLINE uint16_t idemip_tcp_src_port(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_TCP_OFF_SRC_PORT);
}

/** @brief Destination Port. */
PROTOCORE_INLINE uint16_t idemip_tcp_dst_port(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_TCP_OFF_DST_PORT);
}

/** @brief Sequence Number. */
PROTOCORE_INLINE uint32_t idemip_tcp_seq(const uint8_t *h)
{
    return idemip_rd32(h + IDEMIP_TCP_OFF_SEQ);
}

/** @brief Acknowledgment Number; significant only when ACK is set. */
PROTOCORE_INLINE uint32_t idemip_tcp_ack(const uint8_t *h)
{
    return idemip_rd32(h + IDEMIP_TCP_OFF_ACK);
}

/** @brief Data Offset, in 32-bit words. */
PROTOCORE_INLINE uint8_t idemip_tcp_doff(const uint8_t *h)
{
    return (uint8_t)((idemip_rd16(h + IDEMIP_TCP_OFF_OFFS_FLAGS) >> IDEMIP_TCP_DOFF_SHIFT) & IDEMIP_TCP_DOFF_MASK);
}

/** @brief The eight control bits, as the low octet of the offset/flags word. */
PROTOCORE_INLINE uint8_t idemip_tcp_flags(const uint8_t *h)
{
    return (uint8_t)(idemip_rd16(h + IDEMIP_TCP_OFF_OFFS_FLAGS) & 0xFFu);
}

/** @brief Window: octets this sender is willing to accept. */
PROTOCORE_INLINE uint16_t idemip_tcp_window(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_TCP_OFF_WINDOW);
}

/** @brief Checksum as carried. */
PROTOCORE_INLINE uint16_t idemip_tcp_cksum(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_TCP_OFF_CKSUM);
}

/** @brief Urgent Pointer; significant only when URG is set. */
PROTOCORE_INLINE uint16_t idemip_tcp_urgent(const uint8_t *h)
{
    return idemip_rd16(h + IDEMIP_TCP_OFF_URGENT);
}

/**
 * @brief Accumulate the pseudo-header the checksum covers.
 *
 * The same shape as UDP's: the two addresses, a zero octet, the protocol, and the length of the
 * TCP segment - header and data, which is not a field of the header and so is passed in.
 */
PROTOCORE_INLINE uint32_t idemip_tcp_pseudo_accum(uint32_t sum, uint32_t src, uint32_t dst, uint16_t seg_len)
{
    sum += (src >> 16) & 0xFFFFu;
    sum += src & 0xFFFFu;
    sum += (dst >> 16) & 0xFFFFu;
    sum += dst & 0xFFFFu;
    sum += (uint32_t)IDEMIP_IP4_PROTO_TCP; // the zero octet leaves the protocol as the low half
    sum += (uint32_t)seg_len;
    return sum;
}

/**
 * @brief The checksum to write, over the pseudo-header and @p len bytes of segment at @p h.
 *
 * The caller zeroes the checksum field first. Unlike UDP there is no "no checksum" encoding: a
 * result of zero is written as zero.
 */
PROTOCORE_INLINE uint16_t idemip_tcp_cksum_compute(const uint8_t *h, size_t len, uint32_t src, uint32_t dst)
{
    uint32_t sum = idemip_tcp_pseudo_accum(0u, src, dst, (uint16_t)len);
    return idemip_cksum_final(idemip_cksum_accum(sum, h, len));
}

static_assert(IDEMIP_TCP_OFF_OPTIONS == IDEMIP_TCP_HDR_LEN,
              "the RFC 9293 field offsets must sum to the option-free header length");
static_assert(IDEMIP_TCP_HDR_BYTES(IDEMIP_TCP_DOFF_MIN) == IDEMIP_TCP_HDR_LEN,
              "the minimum Data Offset of 5 words is the option-free header (RFC 9293 sec 3.1)");

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_TCP_H
