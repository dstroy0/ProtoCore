// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_datagram.h
 * @brief The UDP wire protocol: what a queued datagram looks like, and how one goes into a ring and
 *        comes back out.
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
 * are the same bytes on every target. The header is built through a pc_span and read through a
 * pc_cspan, which carry the bound and latch an overrun.
 *
 * Both sides of UDP queue the same header, so the layout is stated once here and neither .c restates
 * it. The layout is the contract, so it is published rather than opaque. Internal to transport/udp:
 * no table, no exported symbol.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_DATAGRAM_H
#define PROTOCORE_UDP_DATAGRAM_H

#include "mmgr/bytes.h"           // pc_bw_put / pc_bw_put_be / pc_br_take_be over a span
#include "mmgr/ring.h"            // the SPSC ring the datagrams sit in
#include "shared_primitives/ip.h" // pc_ip: the address a datagram carries, network order

PROTO_BEGIN_DECLS

/** @brief Bytes a queued datagram spends on its header, ahead of the payload. */
#define PC_UDP_DGRAM_HDR 21u

/** @brief Who a queued datagram is from or to, and how long its payload is. */
typedef struct
{
    pc_ip addr;    ///< peer address, network order
    uint16_t port; ///< peer port
    uint16_t len;  ///< payload bytes following the header
} pc_udp_dgram;

/** @brief Write the header of @p d into @p w at its cursor. */
PC_INLINE void pc_udp_dgram_encode(pc_span *w, const pc_udp_dgram *d)
{
    pc_bw_put(w, (uint8_t)d->addr.family);
    pc_bw_put_be(w, d->port, 2);
    pc_bw_put_be(w, d->len, 2);
    pc_bw_put_be(w, pc_rd64be(d->addr.bytes), 8);
    pc_bw_put_be(w, pc_rd64be(d->addr.bytes + 8), 8);
}

/**
 * @brief Read a header out of @p r at its cursor into @p d.
 *
 * A family byte that is neither 4 nor 6 leaves the address empty, so a caller cannot route on a
 * value the parser did not recognize.
 */
PC_INLINE proto_bool pc_udp_dgram_decode(pc_cspan *r, pc_udp_dgram *d)
{
    uint64_t family = 0;
    uint64_t port = 0;
    uint64_t len = 0;
    uint64_t hi = 0;
    uint64_t lo = 0;
    if (!pc_br_take_be(r, 1, &family) || !pc_br_take_be(r, 2, &port) || !pc_br_take_be(r, 2, &len) ||
        !pc_br_take_be(r, 8, &hi) || !pc_br_take_be(r, 8, &lo))
    {
        return PROTO_FALSE;
    }
    d->addr.family = PC_IP_NONE;
    if (family == (uint64_t)PC_IP_V4)
    {
        d->addr.family = PC_IP_V4;
    }
    else if (family == (uint64_t)PC_IP_V6)
    {
        d->addr.family = PC_IP_V6;
    }
    (void)pc_wr64be(d->addr.bytes, hi);
    (void)pc_wr64be(d->addr.bytes + 8, lo);
    d->port = (uint16_t)port;
    d->len = (uint16_t)len;
    return PROTO_TRUE;
}

/**
 * @brief Queue one datagram: header, then @p len payload bytes.
 *
 * @p hdr is caller-owned staging of at least ::PC_UDP_DGRAM_HDR bytes, written only by the producer.
 * Checks the whole entry against the free space first and advances a local head across both spans,
 * publishing it with one store, so a consumer never observes a header without its payload.
 */
PC_INLINE proto_bool pc_udp_dgram_put(uint8_t *ring, size_t cap, _Atomic size_t *head, _Atomic size_t *tail,
                                      uint8_t *hdr, const pc_udp_dgram *d, const uint8_t *payload, size_t len)
{
    if ((PC_UDP_DGRAM_HDR + len) > pc_ring_free(head, tail, cap))
    {
        return PROTO_FALSE;
    }
    pc_span w = pc_span_from(hdr, PC_UDP_DGRAM_HDR);
    pc_udp_dgram_encode(&w, d);
    if (!pc_span_ok(w))
    {
        return PROTO_FALSE;
    }
    size_t h = PROTO_ATOMIC_LOAD(head);
    h = pc_ring_write_span(ring, cap, h, hdr, PC_UDP_DGRAM_HDR);
    h = pc_ring_write_span(ring, cap, h, payload, len);
    PROTO_ATOMIC_STORE(head, h);
    return PROTO_TRUE;
}

/**
 * @brief Dequeue one datagram: @p d takes the header, @p stage takes the payload.
 *
 * @p hdr is caller-owned staging of at least ::PC_UDP_DGRAM_HDR bytes, written only by the consumer.
 * Peeks the header, consumes it, then reads exactly its payload length, so the tail always lands on
 * the next entry boundary. Reports false when the ring holds no whole entry.
 */
PC_INLINE proto_bool pc_udp_dgram_take(uint8_t *ring, size_t cap, _Atomic size_t *head, _Atomic size_t *tail,
                                       uint8_t *hdr, pc_udp_dgram *d, uint8_t *stage, size_t stage_cap)
{
    if (pc_ring_available(head, tail, cap) < PC_UDP_DGRAM_HDR)
    {
        return PROTO_FALSE;
    }
    pc_ring_peek(ring, cap, tail, 0, hdr, PC_UDP_DGRAM_HDR);
    pc_cspan r = pc_cspan_from(hdr, PC_UDP_DGRAM_HDR);
    if (!pc_udp_dgram_decode(&r, d))
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
    if (pc_ring_available(head, tail, cap) < (PC_UDP_DGRAM_HDR + (size_t)d->len))
    {
        return PROTO_FALSE;
    }
    pc_ring_consume(tail, cap, PC_UDP_DGRAM_HDR);
    (void)pc_ring_read(ring, cap, head, tail, stage, d->len);
    return PROTO_TRUE;
}

/** @brief Payload bytes a ring can still take, after the header the next entry costs. */
PC_INLINE size_t pc_udp_dgram_room(const _Atomic size_t *head, const _Atomic size_t *tail, size_t cap)
{
    size_t free_bytes = pc_ring_free(head, tail, cap);
    if (free_bytes <= PC_UDP_DGRAM_HDR)
    {
        return 0; // not even a header fits, so no payload does
    }
    return free_bytes - PC_UDP_DGRAM_HDR;
}

PROTO_END_DECLS

#endif // PROTOCORE_UDP_DATAGRAM_H
