// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ikev2_natt.c
 * @brief IKEv2 NAT traversal (RFC 7296 sec 2.23, RFC 3948 sec 2) - see ikev2_natt.h.
 */

#include "services/security/ikev2/ikev2_natt/ikev2_natt.h"
#include "mmgr/protomem/protomem.h"

#if PROTOCORE_ENABLE_IKEV2

#include "crypto/hash/sha1/sha1.h"
#include "mmgr/secure/secure.h" // the pool the digest borrow comes from
#include "mmgr/span/span.h"     // protocore_span, span.ok

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

PROTOCORE_BEGIN_DECLS

// SPIi(8) | SPIr(8) | IP(4 or 16) | Port(2): the largest detection digest input (RFC 7296 sec 2.23).
#define PROTOCORE_NATD_INPUT_MAX (PROTOCORE_IKE_SPI_LEN + PROTOCORE_IKE_SPI_LEN + 16 + 2)

// ---------------------------------------------------------------------------
// The handle's state
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// NAT detection (RFC 7296 sec 2.23)
// ---------------------------------------------------------------------------

// The SPIs go in first in header order, then the address octets, then the port big endian.
static size_t natd_input(uint8_t *buf, const uint8_t *init_spi, const uint8_t *resp_spi, const uint8_t *ip,
                         size_t ip_len, uint16_t port)
{
    if (!init_spi || !resp_spi || !ip || (ip_len != 4 && ip_len != 16))
    {
        return 0;
    }
    size_t n = 0;
    mem.cpy(buf + n, init_spi, PROTOCORE_IKE_SPI_LEN);
    n += PROTOCORE_IKE_SPI_LEN;
    mem.cpy(buf + n, resp_spi, PROTOCORE_IKE_SPI_LEN);
    n += PROTOCORE_IKE_SPI_LEN;
    mem.cpy(buf + n, ip, ip_len);
    n += ip_len;
    buf[n++] = (uint8_t)(port >> 8);
    buf[n++] = (uint8_t)port;
    return n;
}

static size_t natd_hash(const uint8_t *init_spi, const uint8_t *resp_spi, const uint8_t *ip, size_t ip_len,
                        uint16_t port, uint8_t *out)
{
    if (!out)
    {
        return 0;
    }
    uint8_t in[PROTOCORE_NATD_INPUT_MAX];
    size_t n = natd_input(in, init_spi, resp_spi, ip, ip_len, port);
    if (n == 0)
    {
        return 0;
    }
    const size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_SHA1_BORROW, 8);
    if (!span.ok(w))
    {
        protocore_secure_release(mark);
        return 0;
    }
    Sha1V.hash_args.data = in;
    Sha1V.hash_args.len = n;
    Sha1V.hash_args.out = out;
    Sha1.hash(w.buf);
    protocore_secure_release(mark);
    return PROTOCORE_IKE_NATD_HASH_LEN;
}

// A detection payload is a Notify with Protocol ID and SPI Size zero (RFC 7296 sec 3.10).
static size_t natd_notify_build(uint8_t *restrict work, uint16_t notify_type, const uint8_t *hash)
{
    IkeV.out.buf = IkeNattV.out.buf;
    IkeV.out.cap = IkeNattV.out.cap;
    IkeV.pl.next_payload = IkeNattV.out.next_payload;
    IkeV.pl.data = hash;
    IkeV.pl.data_len = PROTOCORE_IKE_NATD_HASH_LEN;
    IkeV.prop.protocol_id = IKE_PROTO_NONE;
    IkeV.prop.spi = NULL;
    IkeV.prop.spi_size = 0;
    IkeV.notify.notify_type = notify_type;
    Ike.notify_build(work);
    return IkeV.n;
}

// The digest matches when nothing on that axis was translated.
static proto_bool natd_match(uint8_t *restrict work)
{
    if (!IkeNattV.digest.received)
    {
        return PROTO_FALSE;
    }
    uint8_t expect[PROTOCORE_IKE_NATD_HASH_LEN];
    if (natd_hash(IkeNattV.spi.init_spi, IkeNattV.spi.resp_spi, IkeNattV.addr.ip, IkeNattV.addr.ip_len,
                  IkeNattV.addr.port, expect) == 0)
    {
        return PROTO_FALSE;
    }
    return mem.cmp(expect, IkeNattV.digest.received, PROTOCORE_IKE_NATD_HASH_LEN) == 0;
}

void protocore_ike_natt_hash(uint8_t *restrict work)
{
    (void)work;
    IkeNattV.n = natd_hash(IkeNattV.spi.init_spi, IkeNattV.spi.resp_spi, IkeNattV.addr.ip, IkeNattV.addr.ip_len,
                           IkeNattV.addr.port, IkeNattV.digest.out);
}

// The digest covers the address and port this packet was sent from (sec 2.23).
void protocore_ike_natt_source_build(uint8_t *restrict work)
{
    uint8_t h[PROTOCORE_IKE_NATD_HASH_LEN];
    IkeNattV.n = 0;
    if (natd_hash(IkeNattV.spi.init_spi, IkeNattV.spi.resp_spi, IkeNattV.addr.ip, IkeNattV.addr.ip_len,
                  IkeNattV.addr.port, h) == 0)
    {
        return;
    }
    IkeNattV.n = natd_notify_build(work, PROTOCORE_IKE_N_NAT_DETECTION_SOURCE_IP, h);
}

// The digest covers the address and port this packet was sent to (sec 2.23).
void protocore_ike_natt_dest_build(uint8_t *restrict work)
{
    uint8_t h[PROTOCORE_IKE_NATD_HASH_LEN];
    IkeNattV.n = 0;
    if (natd_hash(IkeNattV.spi.init_spi, IkeNattV.spi.resp_spi, IkeNattV.addr.ip, IkeNattV.addr.ip_len,
                  IkeNattV.addr.port, h) == 0)
    {
        return;
    }
    IkeNattV.n = natd_notify_build(work, PROTOCORE_IKE_N_NAT_DETECTION_DESTINATION_IP, h);
}

void protocore_ike_natt_match(uint8_t *restrict work)
{
    IkeNattV.ok = natd_match(work);
}

// No match against the source the packet was observed to come from: someone on the route rewrote it.
void protocore_ike_natt_peer_behind_nat(uint8_t *restrict work)
{
    IkeNattV.ok = !natd_match(work);
}

// No match against our own address: the peer sent to a translated destination.
void protocore_ike_natt_self_behind_nat(uint8_t *restrict work)
{
    IkeNattV.ok = !natd_match(work);
}

// ---------------------------------------------------------------------------
// UDP encapsulation demux on port 4500 (RFC 3948 sec 2)
// ---------------------------------------------------------------------------

void protocore_ike_natt_is_keepalive(uint8_t *restrict work)
{
    (void)work;
    IkeNattV.ok = IkeNattV.pkt.p && IkeNattV.pkt.len == 1 && IkeNattV.pkt.p[0] == PROTOCORE_NATT_KEEPALIVE_BYTE;
}

// The Non-ESP Marker is four zero octets aligned with the ESP SPI, and that SPI is never zero
// (RFC 3948 sec 2.1, sec 2.2), so a leading zero word means the datagram carries IKE.
void protocore_ike_natt_is_ike(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *p = IkeNattV.pkt.p;
    IkeNattV.ok = PROTO_FALSE;
    if (!p || IkeNattV.pkt.len < PROTOCORE_NATT_NON_ESP_MARKER_LEN)
    {
        return;
    }
    IkeNattV.ok = p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
IkeNattVars IkeNattV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IKEV2
