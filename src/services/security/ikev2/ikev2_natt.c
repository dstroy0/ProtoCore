// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ikev2_natt.c
 * @brief IKEv2 NAT traversal (RFC 7296 sec 2.23, RFC 3948 sec 2) - see ikev2_natt.h.
 */

#include "services/security/ikev2/ikev2_natt.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_IKEV2

#include "crypto/hash/sha1.h"

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

// SPIi(8) | SPIr(8) | IP(4 or 16) | Port(2): the largest detection digest input (RFC 7296 sec 2.23).
#define PROTOCORE_NATD_INPUT_MAX (PROTOCORE_IKE_SPI_LEN + PROTOCORE_IKE_SPI_LEN + 16 + 2)

// ---------------------------------------------------------------------------
// The handle's state
// ---------------------------------------------------------------------------

/**
 * @brief The calls that reach this handle - what IkeNattNs points at.
 *
 * @var IkeNattInternal::ns  the handle a caller sets a call's members on
 */
struct IkeNattInternal
{
    IkeNattNs *ns;
};

static struct IkeNattInternal s_natt = {.ns = &IkeNatt};

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
    protocore_sha1(in, n, out);
    return PROTOCORE_IKE_NATD_HASH_LEN;
}

// A detection payload is a Notify with Protocol ID and SPI Size zero (RFC 7296 sec 3.10).
static size_t natd_notify_build(struct IkeNattInternal *restrict ctx, uint16_t notify_type, const uint8_t *hash)
{
    Ike.out.buf = ctx->ns->out.buf;
    Ike.out.cap = ctx->ns->out.cap;
    Ike.pl.next_payload = ctx->ns->out.next_payload;
    Ike.pl.data = hash;
    Ike.pl.data_len = PROTOCORE_IKE_NATD_HASH_LEN;
    Ike.prop.protocol_id = IKE_PROTO_NONE;
    Ike.prop.spi = NULL;
    Ike.prop.spi_size = 0;
    Ike.notify.notify_type = notify_type;
    Ike.notify_build(Ike.internal);
    return Ike.n;
}

// The digest matches when nothing on that axis was translated.
static proto_bool natd_match(struct IkeNattInternal *restrict ctx)
{
    if (!ctx->ns->digest.received)
    {
        return PROTO_FALSE;
    }
    uint8_t expect[PROTOCORE_IKE_NATD_HASH_LEN];
    if (natd_hash(ctx->ns->spi.init_spi, ctx->ns->spi.resp_spi, ctx->ns->addr.ip, ctx->ns->addr.ip_len,
                  ctx->ns->addr.port, expect) == 0)
    {
        return PROTO_FALSE;
    }
    return mem.cmp(expect, ctx->ns->digest.received, PROTOCORE_IKE_NATD_HASH_LEN) == 0;
}

static void hash(struct IkeNattInternal *restrict ctx)
{
    ctx->ns->n = natd_hash(ctx->ns->spi.init_spi, ctx->ns->spi.resp_spi, ctx->ns->addr.ip, ctx->ns->addr.ip_len,
                           ctx->ns->addr.port, ctx->ns->digest.out);
}

// The digest covers the address and port this packet was sent from (sec 2.23).
static void source_build(struct IkeNattInternal *restrict ctx)
{
    uint8_t h[PROTOCORE_IKE_NATD_HASH_LEN];
    ctx->ns->n = 0;
    if (natd_hash(ctx->ns->spi.init_spi, ctx->ns->spi.resp_spi, ctx->ns->addr.ip, ctx->ns->addr.ip_len,
                  ctx->ns->addr.port, h) == 0)
    {
        return;
    }
    ctx->ns->n = natd_notify_build(ctx, PROTOCORE_IKE_N_NAT_DETECTION_SOURCE_IP, h);
}

// The digest covers the address and port this packet was sent to (sec 2.23).
static void dest_build(struct IkeNattInternal *restrict ctx)
{
    uint8_t h[PROTOCORE_IKE_NATD_HASH_LEN];
    ctx->ns->n = 0;
    if (natd_hash(ctx->ns->spi.init_spi, ctx->ns->spi.resp_spi, ctx->ns->addr.ip, ctx->ns->addr.ip_len,
                  ctx->ns->addr.port, h) == 0)
    {
        return;
    }
    ctx->ns->n = natd_notify_build(ctx, PROTOCORE_IKE_N_NAT_DETECTION_DESTINATION_IP, h);
}

static void match(struct IkeNattInternal *restrict ctx)
{
    ctx->ns->ok = natd_match(ctx);
}

// No match against the source the packet was observed to come from: someone on the route rewrote it.
static void peer_behind_nat(struct IkeNattInternal *restrict ctx)
{
    ctx->ns->ok = !natd_match(ctx);
}

// No match against our own address: the peer sent to a translated destination.
static void self_behind_nat(struct IkeNattInternal *restrict ctx)
{
    ctx->ns->ok = !natd_match(ctx);
}

// ---------------------------------------------------------------------------
// UDP encapsulation demux on port 4500 (RFC 3948 sec 2)
// ---------------------------------------------------------------------------

static void is_keepalive(struct IkeNattInternal *restrict ctx)
{
    ctx->ns->ok = ctx->ns->pkt.p && ctx->ns->pkt.len == 1 && ctx->ns->pkt.p[0] == PROTOCORE_NATT_KEEPALIVE_BYTE;
}

// The Non-ESP Marker is four zero octets aligned with the ESP SPI, and that SPI is never zero
// (RFC 3948 sec 2.1, sec 2.2), so a leading zero word means the datagram carries IKE.
static void is_ike(struct IkeNattInternal *restrict ctx)
{
    const uint8_t *p = ctx->ns->pkt.p;
    ctx->ns->ok = PROTO_FALSE;
    if (!p || ctx->ns->pkt.len < PROTOCORE_NATT_NON_ESP_MARKER_LEN)
    {
        return;
    }
    ctx->ns->ok = p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0;
}

// Designated, so a member's position in the struct does not decide what it binds to.
IkeNattNs IkeNatt = {.hash = hash,
                     .source_build = source_build,
                     .dest_build = dest_build,
                     .match = match,
                     .peer_behind_nat = peer_behind_nat,
                     .self_behind_nat = self_behind_nat,
                     .is_keepalive = is_keepalive,
                     .is_ike = is_ike,
                     .internal = &s_natt};

#endif // PROTOCORE_ENABLE_IKEV2
