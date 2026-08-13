// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file common.h
 * @brief The UDP wire protocol: what a received datagram looks like in the ring, and how one goes
 *        in and comes back out.
 *
 * A datagram is a message, so a ring of them carries a fixed 21-byte header ahead of each payload
 * rather than a byte stream:
 *
 *     offset  width  field
 *     0       1      family   4 for IPv4, 6 for IPv6
 *     1       2      port     big-endian
 *     3       2      len      big-endian, payload bytes that follow the header
 *     5       16     addr     network order, IPv4 in the first four
 *     21      len    payload
 *
 * Every field is written and read at a stated width in network byte order, so the bytes in the ring
 * are the same bytes on every target. The header is built through a protocore_span and read through a
 * protocore_cspan, which carry the bound and latch an overrun.
 *
 * The layout is the contract, so it is published rather than opaque. Internal to transport/udp: no
 * table, no exported symbol.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_COMMON_H
#define PROTOCORE_UDP_COMMON_H

#include "mmgr/bytes.h"           // bytes.put / bytes.put_be / bytes.take_be over a span
#include "mmgr/ring.h"            // the SPSC ring the datagrams sit in
#include "shared/ip/ip.h" // protocore_ip: the address a datagram carries, network order

PROTOCORE_BEGIN_DECLS

/** @brief Bytes a queued datagram spends on its header, ahead of the payload. */
#define PROTOCORE_UDP_DGRAM_HDR 21u

/** @brief Who a queued datagram is from or to, and how long its payload is. */
typedef struct
{
    protocore_ip addr; ///< peer address, network order
    uint16_t port;     ///< peer port
    uint16_t len;      ///< payload bytes following the header
} protocore_udp_dgram;

/** @brief Write the header of @p d into @p w at its cursor. */
PROTOCORE_INLINE void protocore_udp_dgram_encode(protocore_span *w, const protocore_udp_dgram *d)
{
    bytes.put(w, (uint8_t)d->addr.family);
    bytes.put_be(w, d->port, 2);
    bytes.put_be(w, d->len, 2);
    bytes.put_be(w, endian.rd64be(d->addr.bytes), 8);
    bytes.put_be(w, endian.rd64be(d->addr.bytes + 8), 8);
}

/**
 * @brief Read a header out of @p r at its cursor into @p d.
 *
 * A family byte that is neither 4 nor 6 leaves the address empty, so a caller cannot route on a
 * value the parser did not recognize.
 */
PROTOCORE_INLINE proto_bool protocore_udp_dgram_decode(protocore_cspan *r, protocore_udp_dgram *d)
{
    uint64_t family = 0;
    uint64_t port = 0;
    uint64_t len = 0;
    uint64_t hi = 0;
    uint64_t lo = 0;
    if (!bytes.take_be(r, 1, &family) || !bytes.take_be(r, 2, &port) || !bytes.take_be(r, 2, &len) ||
        !bytes.take_be(r, 8, &hi) || !bytes.take_be(r, 8, &lo))
    {
        return PROTO_FALSE;
    }
    d->addr.family = PROTOCORE_IP_NONE;
    if (family == (uint64_t)PROTOCORE_IP_V4)
    {
        d->addr.family = PROTOCORE_IP_V4;
    }
    else if (family == (uint64_t)PROTOCORE_IP_V6)
    {
        d->addr.family = PROTOCORE_IP_V6;
    }
    (void)endian.wr64be(d->addr.bytes, hi);
    (void)endian.wr64be(d->addr.bytes + 8, lo);
    d->port = (uint16_t)port;
    d->len = (uint16_t)len;
    return PROTO_TRUE;
}

/**
 * @brief Dequeue one datagram: @p d takes the header, @p stage takes the payload.
 *
 * @p hdr is caller-owned staging of at least ::PROTOCORE_UDP_DGRAM_HDR bytes, written only by the consumer.
 * Peeks the header, consumes it, then reads exactly its payload length, so the tail always lands on
 * the next entry boundary. Reports false when the ring holds no whole entry.
 */
PROTOCORE_INLINE proto_bool protocore_udp_dgram_take(uint8_t *ring, size_t cap, _Atomic size_t *head,
                                                     _Atomic size_t *tail, uint8_t *hdr, protocore_udp_dgram *d,
                                                     uint8_t *stage, size_t stage_cap)
{
    if (protocore_ring_available(head, tail, cap) < PROTOCORE_UDP_DGRAM_HDR)
    {
        return PROTO_FALSE;
    }
    protocore_ring_peek(ring, cap, tail, 0, hdr, PROTOCORE_UDP_DGRAM_HDR);
    protocore_cspan r = span.cfrom(hdr, PROTOCORE_UDP_DGRAM_HDR);
    if (!protocore_udp_dgram_decode(&r, d))
    {
        return PROTO_FALSE;
    }
    if (d->len > stage_cap)
    {
        // Nothing queues a payload longer than the stage, so a length past it means the ring lost its
        // entry boundary. Drop the whole ring rather than read past one.
        PROTO_ATOMIC_STORE(tail, PROTO_ATOMIC_LOAD(head));
        return PROTO_FALSE;
    }
    if (protocore_ring_available(head, tail, cap) < (PROTOCORE_UDP_DGRAM_HDR + (size_t)d->len))
    {
        return PROTO_FALSE;
    }
    protocore_ring_consume(tail, cap, PROTOCORE_UDP_DGRAM_HDR);
    (void)protocore_ring_read(ring, cap, head, tail, stage, d->len);
    return PROTO_TRUE;
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_UDP_COMMON_H
