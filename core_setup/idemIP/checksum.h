// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file checksum.h
 * @brief The internet checksum, RFC 1071.
 *
 * "the 16-bit 1's complement of the 1's complement sum of all 16-bit words". Every header here
 * carries one: the IPv4 header over itself (RFC 791 sec 3.1), ICMP over its message (RFC 792), and
 * TCP and UDP over a pseudo-header and their payload.
 *
 * Reads the caller's bytes and returns a number. Holds nothing.
 */

#ifndef PROTOCORE_IDEMIP_CHECKSUM_H
#define PROTOCORE_IDEMIP_CHECKSUM_H

#include "idemIP/endian.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief Accumulate @p len bytes at @p p into a running 1's complement sum.
 *
 * RFC 1071 sec 1: the words are [A,B] +' [C,D] +' ... , and an odd count is the final byte taken
 * as [Z,0] - the pad is not sent, it only completes the last word.
 *
 * The carries are folded once at the end rather than per word: RFC 1071 sec 1 notes "any overflows
 * from the most significant bits are added into the least significant bits", and sec 2 that the
 * sum may be accumulated deferred and folded afterwards, which is the same value in fewer steps.
 * A 32-bit accumulator cannot overflow before folding for any length this stack carries.
 *
 * Bytes are read one at a time and paired with a shift, so the span's alignment never matters -
 * a header lands wherever the one before it ended.
 *
 * @param sum running sum from a previous call, or 0 to begin.
 */
PROTOCORE_INLINE uint32_t idemip_cksum_accum(uint32_t sum, const uint8_t *p, size_t len)
{
    size_t i = 0;
    while (i + 1u < len)
    {
        sum += (uint32_t)(((uint32_t)p[i] << 8) | (uint32_t)p[i + 1u]);
        i += 2u;
    }
    if (i < len)
    {
        sum += (uint32_t)((uint32_t)p[i] << 8); // [Z,0]: the odd byte is the high half
    }
    return sum;
}

/**
 * @brief Fold the carries and complement, giving the value that goes in the header.
 *
 * RFC 1071 sec 1: the end-around carry is applied until none remains, then the sum is complemented.
 * Two folds are enough - the first can produce at most one new carry.
 */
PROTOCORE_INLINE uint16_t idemip_cksum_final(uint32_t sum)
{
    sum = (sum & 0xFFFFu) + (sum >> 16);
    sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFu);
}

/** @brief The checksum over one span. */
PROTOCORE_INLINE uint16_t idemip_cksum(const uint8_t *p, size_t len)
{
    return idemip_cksum_final(idemip_cksum_accum(0u, p, len));
}

/**
 * @brief True when a received span checks out.
 *
 * RFC 1071 sec 1: summing a span that already carries its checksum yields all ones, whose
 * complement is zero. A verifier therefore runs the same sum and tests for zero rather than
 * clearing the field and recomputing.
 */
PROTOCORE_INLINE proto_bool idemip_cksum_valid(const uint8_t *p, size_t len)
{
    return idemip_cksum(p, len) == 0u;
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_CHECKSUM_H
