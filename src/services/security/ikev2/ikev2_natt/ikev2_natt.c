// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ikev2_natt.c
 * @brief IKEv2 NAT traversal (RFC 7296 sec 2.23, RFC 3948 sec 2) - see ikev2_natt.h.
 */

#include "services/security/ikev2/ikev2_natt/ikev2_natt.h"
#include "mmgr/protomem/protomem.h"

static uint8_t ikev2_work[16]; // the borrow an entry takes; Ike never reads it

#if PROTOCORE_ENABLE_IKEV2

#include "crypto/hash/sha1/sha1.h"
#include "mmgr/secure/secure.h" // the pool the digest borrow comes from
#include "mmgr/span/span.h"   // protocore_span, span.ok

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

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
    Sha1.hash_args.data = in;
    Sha1.hash_args.len = n;
    Sha1.hash_args.out = out;
    Sha1.hash(w.buf);
    protocore_secure_release(mark);
    return PROTOCORE_IKE_NATD_HASH_LEN;
}

// A detection payload is a Notify with Protocol ID and SPI Size zero (RFC 7296 sec 3.10).
static size_t natd_notify_build(uint8_t *restrict work, uint16_t notify_type, const uint8_t *hash)
{
    Ike.out.buf = IkeNatt.out.buf;
    Ike.out.cap = IkeNatt.out.cap;
    Ike.pl.next_payload = IkeNatt.out.next_payload;
    Ike.pl.data = hash;
    Ike.pl.data_len = PROTOCORE_IKE_NATD_HASH_LEN;
    Ike.prop.protocol_id = IKE_PROTO_NONE;
    Ike.prop.spi = NULL;
    Ike.prop.spi_size = 0;
    Ike.notify.notify_type = notify_type;
    Ike.notify_build(ikev2_work);
    return Ike.n;
}

// The digest matches when nothing on that axis was translated.
static proto_bool natd_match(uint8_t *restrict work)
{
    if (!IkeNatt.digest.received)
    {
        return PROTO_FALSE;
    }
    uint8_t expect[PROTOCORE_IKE_NATD_HASH_LEN];
    if (natd_hash(IkeNatt.spi.init_spi, IkeNatt.spi.resp_spi, IkeNatt.addr.ip, IkeNatt.addr.ip_len, IkeNatt.addr.port,
                  expect) == 0)
    {
        return PROTO_FALSE;
    }
    return mem.cmp(expect, IkeNatt.digest.received, PROTOCORE_IKE_NATD_HASH_LEN) == 0;
}

static void hash(uint8_t *restrict work)
{
    (void)work;
    IkeNatt.n = natd_hash(IkeNatt.spi.init_spi, IkeNatt.spi.resp_spi, IkeNatt.addr.ip, IkeNatt.addr.ip_len,
                          IkeNatt.addr.port, IkeNatt.digest.out);
}

// The digest covers the address and port this packet was sent from (sec 2.23).
static void source_build(uint8_t *restrict work)
{
    uint8_t h[PROTOCORE_IKE_NATD_HASH_LEN];
    IkeNatt.n = 0;
    if (natd_hash(IkeNatt.spi.init_spi, IkeNatt.spi.resp_spi, IkeNatt.addr.ip, IkeNatt.addr.ip_len, IkeNatt.addr.port,
                  h) == 0)
    {
        return;
    }
    IkeNatt.n = natd_notify_build(work, PROTOCORE_IKE_N_NAT_DETECTION_SOURCE_IP, h);
}

// The digest covers the address and port this packet was sent to (sec 2.23).
static void dest_build(uint8_t *restrict work)
{
    uint8_t h[PROTOCORE_IKE_NATD_HASH_LEN];
    IkeNatt.n = 0;
    if (natd_hash(IkeNatt.spi.init_spi, IkeNatt.spi.resp_spi, IkeNatt.addr.ip, IkeNatt.addr.ip_len, IkeNatt.addr.port,
                  h) == 0)
    {
        return;
    }
    IkeNatt.n = natd_notify_build(work, PROTOCORE_IKE_N_NAT_DETECTION_DESTINATION_IP, h);
}

static void match(uint8_t *restrict work)
{
    IkeNatt.ok = natd_match(work);
}

// No match against the source the packet was observed to come from: someone on the route rewrote it.
static void peer_behind_nat(uint8_t *restrict work)
{
    IkeNatt.ok = !natd_match(work);
}

// No match against our own address: the peer sent to a translated destination.
static void self_behind_nat(uint8_t *restrict work)
{
    IkeNatt.ok = !natd_match(work);
}

// ---------------------------------------------------------------------------
// UDP encapsulation demux on port 4500 (RFC 3948 sec 2)
// ---------------------------------------------------------------------------

static void is_keepalive(uint8_t *restrict work)
{
    (void)work;
    IkeNatt.ok = IkeNatt.pkt.p && IkeNatt.pkt.len == 1 && IkeNatt.pkt.p[0] == PROTOCORE_NATT_KEEPALIVE_BYTE;
}

// The Non-ESP Marker is four zero octets aligned with the ESP SPI, and that SPI is never zero
// (RFC 3948 sec 2.1, sec 2.2), so a leading zero word means the datagram carries IKE.
static void is_ike(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *p = IkeNatt.pkt.p;
    IkeNatt.ok = PROTO_FALSE;
    if (!p || IkeNatt.pkt.len < PROTOCORE_NATT_NON_ESP_MARKER_LEN)
    {
        return;
    }
    IkeNatt.ok = p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0;
}

// Designated, so a member's position in the struct does not decide what it binds to.
IkeNattNs IkeNatt = {.hash = hash,
                     .source_build = source_build,
                     .dest_build = dest_build,
                     .match = match,
                     .peer_behind_nat = peer_behind_nat,
                     .self_behind_nat = self_behind_nat,
                     .is_keepalive = is_keepalive,
                     .is_ike = is_ike};

#endif // PROTOCORE_ENABLE_IKEV2
