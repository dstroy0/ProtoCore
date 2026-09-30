// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * are the same bytes on every target. The header is built through an mmgr_span and read through an
 * mmgr_cspan, which carry the bound and latch an overrun.
 *
 * The layout is the contract, so it is published rather than opaque. Internal to transport/udp: no
 * table, no exported symbol.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_COMMON_H
#define PROTOCORE_UDP_COMMON_H

#include "endian/endian.h"                                   // magna_extremitas: the address as two big-endian words
#include "memoria_anularis/memoria_anularis.h"               // mmgr_ring: the SPSC ring the datagrams sit in
#include "octetus_introitus_exitus/octetus_introitus_exitus.h" // byteio.put / put_be / take_be over a span
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
PROTOCORE_INLINE void protocore_udp_dgram_encode(mmgr_span *w, const protocore_udp_dgram *d)
{
    EMBED_CALL(byteio.put, OctetusCfg, .write_span = w, .byte = (uint8_t)d->addr.family);
    EMBED_CALL(byteio.put_be, OctetusCfg, .write_span = w, .value = d->port, .bytes = 2);
    EMBED_CALL(byteio.put_be, OctetusCfg, .write_span = w, .value = d->len, .bytes = 2);
    EMBED_CALL(byteio.put_be, OctetusCfg, .write_span = w,
               .value = EMBED_CALL(magna_extremitas.rd, EndianCfg, .src = d->addr.bytes, .width = MMGR_ENDIAN_64),
               .bytes = 8);
    EMBED_CALL(byteio.put_be, OctetusCfg, .write_span = w,
               .value = EMBED_CALL(magna_extremitas.rd, EndianCfg, .src = d->addr.bytes + 8, .width = MMGR_ENDIAN_64),
               .bytes = 8);
}

/**
 * @brief Read a header out of @p r at its cursor into @p d.
 *
 * A family byte that is neither 4 nor 6 leaves the address empty, so a caller cannot route on a
 * value the parser did not recognize.
 */
PROTOCORE_INLINE proto_bool protocore_udp_dgram_decode(mmgr_cspan *r, protocore_udp_dgram *d)
{
    uint64_t family = 0;
    uint64_t port = 0;
    uint64_t len = 0;
    uint64_t hi = 0;
    uint64_t lo = 0;
    if (!EMBED_CALL(byteio.take_be, OctetusCfg, .read_span = r, .bytes = 1, .out = &family) ||
        !EMBED_CALL(byteio.take_be, OctetusCfg, .read_span = r, .bytes = 2, .out = &port) ||
        !EMBED_CALL(byteio.take_be, OctetusCfg, .read_span = r, .bytes = 2, .out = &len) ||
        !EMBED_CALL(byteio.take_be, OctetusCfg, .read_span = r, .bytes = 8, .out = &hi) ||
        !EMBED_CALL(byteio.take_be, OctetusCfg, .read_span = r, .bytes = 8, .out = &lo))
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
    (void)EMBED_CALL(magna_extremitas.wr, EndianCfg, .dst = d->addr.bytes, .val = hi, .width = MMGR_ENDIAN_64);
    (void)EMBED_CALL(magna_extremitas.wr, EndianCfg, .dst = d->addr.bytes + 8, .val = lo, .width = MMGR_ENDIAN_64);
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
PROTOCORE_INLINE proto_bool protocore_udp_dgram_take(mmgr_ring *ring, uint8_t *hdr, protocore_udp_dgram *d,
                                                     uint8_t *stage, size_t stage_cap)
{
    if (EMBED_CALL(anularis.available, AnularisCfg, .ring = ring) < PROTOCORE_UDP_DGRAM_HDR)
    {
        return PROTO_FALSE;
    }
    EMBED_CALL(anularis.peek, AnularisCfg, .ring = ring, .dst = hdr, .bytes = PROTOCORE_UDP_DGRAM_HDR, .offset = 0);
    mmgr_cspan r = {.buf = hdr, .len = PROTOCORE_UDP_DGRAM_HDR, .pos = 0, .err = EMBED_FALSE};
    if (!protocore_udp_dgram_decode(&r, d))
    {
        return PROTO_FALSE;
    }
    if (d->len > stage_cap)
    {
        // Nothing queues a payload longer than the stage, so a length past it means the ring lost its
        // entry boundary. Drop the whole ring rather than read past one.
        EMBED_CALL(anularis.consume, AnularisCfg, .ring = ring,
                   .bytes = EMBED_CALL(anularis.available, AnularisCfg, .ring = ring));
        return PROTO_FALSE;
    }
    if (EMBED_CALL(anularis.available, AnularisCfg, .ring = ring) < (PROTOCORE_UDP_DGRAM_HDR + (size_t)d->len))
    {
        return PROTO_FALSE;
    }
    EMBED_CALL(anularis.consume, AnularisCfg, .ring = ring, .bytes = PROTOCORE_UDP_DGRAM_HDR);
    (void)EMBED_CALL(anularis.read, AnularisCfg, .ring = ring, .dst = stage, .bytes = d->len);
    return PROTO_TRUE;
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_UDP_COMMON_H
